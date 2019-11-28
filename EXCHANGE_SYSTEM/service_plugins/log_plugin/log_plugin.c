#include <errno.h>
#include <unistd.h>
#include <gromox/defs.h>
#include "log_plugin.h"
#include "config_file.h"
#include "util.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <pthread.h>

#define LOG_LEN						4096

enum{
	REDIRECT_FAIL_OPEN,
	REDIRECT_ALREADY_OPEN,
	REDIRECT_OPEN_OK
};

enum{
	REDIRECT_FAIL_CLOSE,
	REDIRECT_NO_OPEN,
	REDIRECT_CLOSE_OK
};

static BOOL g_notify_stop = TRUE;
static char *g_log_buf_ptr;
static int g_current_size;
static int g_log_buf_size;
static int g_log_level;
static int g_files_num;
static char *g_files_name;
static FILE *g_redirect_fp;
static char g_redirect_name[256];
static char g_config_path[256];
static char g_file_name[256];
static char g_file_postfix[256];
static char g_log_dir[256];
static pthread_t g_thread_id;
static pthread_mutex_t g_buffer_lock;
static pthread_mutex_t g_redirect_lock;

static void log_plugin_cache_log(const char *log, int length);
static BOOL log_plugin_flush_log(void);
static int log_plugin_open_redirect(const char *filename);
static int log_plugin_close_redirect(void);
static void* thread_work_func(void *arg);

/*
 *	log plugin's construct function
 *	@param
 *		cache_size		size of cache
 */
void log_plugin_init(const char *config_path, const char* log_file_name,
	int log_level, int files_num, int cache_size)
{
	char *psearch;
	
	strcpy(g_config_path, config_path);
	g_log_level = log_level;
	g_files_num = files_num;
	g_redirect_fp = NULL;
	strcpy(g_redirect_name, "off");
	g_current_size = 0;
	g_log_buf_ptr = NULL;
	g_log_buf_size = cache_size;
	psearch = strrchr(log_file_name, '.');
	if (NULL == psearch) {
		strcpy(g_file_name, log_file_name);
		strcpy(g_file_postfix, "txt");
	} else {
		memcpy(g_file_name, log_file_name, psearch - log_file_name);
		g_file_name[psearch - log_file_name] = '\0';
		strcpy(g_file_postfix, psearch + 1);
	}
	psearch = strrchr(log_file_name, '/');
	if (NULL == psearch) {
		strcpy(g_log_dir, ".");
	} else {
		memcpy(g_log_dir, log_file_name, psearch - log_file_name);
		g_log_dir[psearch - log_file_name] = '\0';
	}
	
	pthread_mutex_init(&g_buffer_lock, NULL);
	pthread_mutex_init(&g_redirect_lock, NULL);
}

/*
 *	log plugin's destruct function
 */
void log_plugin_free()
{
	pthread_mutex_destroy(&g_buffer_lock);
	pthread_mutex_destroy(&g_redirect_lock);
}

/*
 *  run log plugin
 *  @return
 *		 0		success
 *		<>0		fail
 */
int log_plugin_run()
{
	pthread_attr_t  attr;
	
	g_log_buf_ptr = malloc(g_log_buf_size);
	if (NULL == g_log_buf_ptr) {
		printf("[log_plugin]: fail to allocate memory for cache buffer\n");
		return -1;
	}
	g_files_name = malloc(256*g_files_num);
	if (NULL == g_files_name) {
		printf("[log_plugin]: fail to allocate memory for files name buffer\n");
		return -2;
	}
	g_notify_stop = FALSE;
	pthread_attr_init(&attr);
	int ret = pthread_create(&g_thread_id, &attr, thread_work_func, nullptr);
	if (ret != 0) {
		pthread_attr_destroy(&attr);
		printf("[log_plugin]: failed to create thread: %s\n", strerror(ret));
		return -3;
	}
	pthread_setname_np(g_thread_id, "log_plugin");
	pthread_attr_destroy(&attr);
	return 0;

}

/*
 *	stop log plugin
 *	@return
 *		 0		success
 *		<>0		fail
 */
int log_plugin_stop()
{
	if (FALSE == g_notify_stop) {
		g_notify_stop = TRUE;
		pthread_join(g_thread_id, NULL);
	}
	if (NULL != g_log_buf_ptr) {
		log_plugin_flush_log();
		free(g_log_buf_ptr);
		g_log_buf_ptr = NULL;
	}
	if (NULL != g_files_name) {
		free(g_files_name);
		g_files_name = NULL;
	}
	return 0;
}

/*
 *	log info service string
 *	@param
 *		level			log level
 *		format [in]		format string
 */
void log_plugin_log_info(int level, const char *format, ...)
{
	char log_buf[LOG_LEN];
	time_t time_now;
	struct tm *tm_time_now;
	struct tm time_buff;
	va_list ap; 
	int len;
	int fd;
	

	if (level < g_log_level) {
		return; /* ignore the low level logs */
	}
	time(&time_now);
	tm_time_now = localtime_r(&time_now, &time_buff);
	len = strftime(log_buf, LOG_LEN, "%Y/%m/%d %H:%M:%S\t", tm_time_now);
			
	va_start(ap, format);
	len += vsnprintf(log_buf + len, sizeof(log_buf) - len - 1, format, ap);
	log_buf[len++]  = '\n';
	log_plugin_cache_log(log_buf, len);

	/* log redirect operation */
	pthread_mutex_lock(&g_redirect_lock);
	if (NULL != g_redirect_fp) {
		fd = fileno(g_redirect_fp);
		write(fd, log_buf, len);
		fsync(fd);
	}
	pthread_mutex_unlock(&g_redirect_lock);
}

/*
 *	clean up smtp and delivery logs over 30days
 */
static void* thread_work_func(void *arg)
{
	int i;
	BOOL should_delete;
	time_t current_time, tmp_time;
	struct tm *tm_time;
	struct tm time_buff;
	char time_str[32];
	char temp_path[256];
	DIR *dirp;
	struct dirent *direntp;

	while (FALSE == g_notify_stop) {
		time(&current_time);
		for (i=0; i<g_files_num; i++) {
			tmp_time = current_time - i * 24 * 3600;
			tm_time = localtime_r(&tmp_time, &time_buff);
			strftime(time_str, 32, "%m%d", tm_time);
			snprintf(g_files_name + i * 256, 256, "%s%s.%s", g_file_name,
				time_str, g_file_postfix);
		}
		dirp = opendir(g_log_dir);
		if (NULL == dirp) {
			goto WAIT_CLEAN;
		}
		while ((direntp = readdir(dirp)) != NULL) {
			if (0 == strcmp(direntp->d_name, ".") ||
				0 == strcmp(direntp->d_name, "..")) {
				continue;
			}
			should_delete = TRUE;
			for (i=0; i<g_files_num; i++) {
				sprintf(temp_path, "%s/%s", g_log_dir, direntp->d_name);
				if (0 == strcmp(temp_path, g_files_name + i * 256)) {
					should_delete = FALSE;
					break;
				}
			}
			if (TRUE == should_delete) {
				remove(temp_path);
			}
		}
		closedir(dirp);
WAIT_CLEAN:
		for (i=0; i<24*3600; i++) {
			if (TRUE == g_notify_stop) {
				pthread_exit(0);
			}
			sleep(1);
		}
	}
	return NULL;
}

/*
 *	log plugin's console talk function
 *	@param
 *		argc			argument number
 *		argv [in]		arguments value
 *		result [out]	buffer for passing out result
 *		length			result buffer length
 */
void log_plugin_console_talk(int argc, char **argv, char *result, int length)
{
	BOOL flush_result;
	CONFIG_FILE *pfile;
	int log_level;
	int valid_days;
	char temp_buff[64];
	char help_string[] = "250 log plugin help information:\r\n"
						 "\t%s info\r\n"
						 "\t    --print log plugin information\r\n"
						 "\t%s set level <0~8>\r\n"
						 "\t    --set log level\r\n"
						 "\t%s set valid-days <num>\r\n"
						 "\t    --set valid days of log files\r\n"
						 "\t%s open <file_name>\r\n"
						 "\t    --redirect real-time log into file_name\r\n"
						 "\t%s close\r\n"
						 "\t    --close the redirected log file\r\n"
						 "\t%s flush\r\n"
						 "\t    --flush cached log in to log file";
	
	if (1 == argc) {
		strncpy(result, "550 too few arguments", length);
		return;
	}
	if (2 == argc && 0 == strcmp("--help", argv[1])) {
		snprintf(result, length, help_string, argv[0], argv[0], argv[0],
				argv[0], argv[0], argv[0]);
		result[length - 1] ='\0';
		return;
	}
	if (2 == argc && 0 == strcmp("info", argv[1])) {
		bytetoa(g_log_buf_size, temp_buff);
		snprintf(result, length, 
					"250 log plugin information:\r\n"
					"\tlog level       %d\r\n"
					"\tcache size      %s\r\n"
					"\tlog redirect    %s",
					g_log_level, temp_buff, g_redirect_name);
		return;
	}
	if (2 == argc && 0 == strcmp("close", argv[1])) {
		switch (log_plugin_close_redirect()) {
		case REDIRECT_FAIL_CLOSE:
			snprintf(result, length, "550 fail to close redirected log file %s",
					g_redirect_name);
			return;
		case REDIRECT_NO_OPEN:
			strncpy(result,"550 no redirected log file has been opened",length);
			return;
		case REDIRECT_CLOSE_OK:
			strncpy(result, "250 close redirected log file OK", length);
			return;
		}
	}
	if (2 == argc && 0 == strcmp("flush", argv[1])) {
		pthread_mutex_lock(&g_buffer_lock);
		flush_result = log_plugin_flush_log();
		pthread_mutex_unlock(&g_buffer_lock);
		if (FALSE == flush_result) {
			strncpy(result, "550 flush failed", length);
		} else {
			strncpy(result, "250 flush OK", length);
		}
		return;
	}
	if (4 == argc && 0 == strcmp("set", argv[1])) {
		if (0 == strcmp("level", argv[2])) {
			log_level = atoi(argv[3]);
			if (log_level > 8 || log_level < 0) {
				strncpy(result, "550 level should between 0 and 8", length);
				return;
			}
			pfile = config_file_init2(NULL, g_config_path);
			if (NULL == pfile) {
				strncpy(result, "550 fail to open config file", length);
				return;
			}
			g_log_level = log_level;
			config_file_set_value(pfile, "LOG_LEVEL", argv[3]);
			config_file_save(pfile);
			config_file_free(pfile);
			strncpy(result, "250 set log level OK", length);
			return;
		} else if (0 == strcmp("valid-days", argv[2])) {
			valid_days = atoi(argv[3]);
			if (valid_days <= 0) {
				strncpy(result, "550 level should large than 0", length);
				return;
			}
			pfile = config_file_init2(NULL, g_config_path);
			if (NULL == pfile) {
				strncpy(result, "550 fail to open config file", length);
				return;
			}
			g_files_num = valid_days;
			config_file_set_value(pfile, "FILES_NUM", argv[3]);
			config_file_save(pfile);
			config_file_free(pfile);
			strncpy(result, "250 set valid days OK", length);
			return;
		}
	}
	if (3 == argc && 0 == strcmp("open", argv[1])) {
		switch (log_plugin_open_redirect(argv[2])) {
		case REDIRECT_FAIL_OPEN:
			snprintf(result, length, "550 fail to open redirected log file %s",
					argv[2]);
			return;
		case REDIRECT_ALREADY_OPEN:
			snprintf(result, length, "550 %s is already opened", argv[2]);
			return;
		case REDIRECT_OPEN_OK:
			strncpy(result, "250 open redirected log file OK", length);
			return;
		}
	}
	snprintf(result, length, "550 invalid argument %s", argv[1]);
	return;
}

/*
 *	redirect log into file
 *	@param
 *		filename [in]			file name
 *	@return
 *		REDIRECT_FAIL_OPEN		fail to open file
 *		REDIRECT_ALREADY_OPEN	file already opened
 *		REDIRECT_OPEN_OK		OK to redirect
 */
static int log_plugin_open_redirect(const char *filename)
{
	FILE *fp;
	
	if (NULL != g_redirect_fp) {
		return REDIRECT_ALREADY_OPEN;
	}
	fp = fopen(filename, "a+");
	if (NULL == fp) {
		return REDIRECT_FAIL_OPEN;
	}
	strcpy(g_redirect_name, filename);
	pthread_mutex_lock(&g_redirect_lock);
	g_redirect_fp = fp;
	pthread_mutex_unlock(&g_redirect_lock);
	return REDIRECT_OPEN_OK;
}

/*
 *	close redirect log
 *	@return
 *		REDIRECT_FAIL_CLOSE		fail to close file
 *		REDIRECT_NO_OPEN		no redirect opened
 *		REDIRECT_CLOSE_OK		OK to close redirect
 */
static int log_plugin_close_redirect()
{
	int ret_value;
	
	if (NULL == g_redirect_fp) {
		return REDIRECT_NO_OPEN;
	}
	
	strcpy(g_redirect_name, "off");
	pthread_mutex_lock(&g_redirect_lock);
	if (0 != fclose(g_redirect_fp)) {
		ret_value = REDIRECT_FAIL_CLOSE;
	} else {
		ret_value = REDIRECT_CLOSE_OK;
		g_redirect_fp = NULL;
	}
	pthread_mutex_unlock(&g_redirect_lock);
	return ret_value;
}

/*
 *	cache log string into buffer, and if the buffer is full, write them into 
 *	log file and empty the buffer
 *	@param
 *		log [in]		log string, without '\0'
 *		length			length of string
 */
static void log_plugin_cache_log(const char *log, int length)
{
		   
	if (g_current_size + length > g_log_buf_size) {
		pthread_mutex_lock(&g_buffer_lock);
		if (g_current_size + length > g_log_buf_size) {
			log_plugin_flush_log();
		}
		pthread_mutex_unlock(&g_buffer_lock);
	}
	pthread_mutex_lock(&g_buffer_lock);
	memcpy(g_log_buf_ptr + g_current_size, log, length);
	g_current_size += length;
	pthread_mutex_unlock(&g_buffer_lock);
}

/*
 * Flush cached log into log file. Use mutex lock when invoking this function.
 */
static BOOL log_plugin_flush_log()
{
	char time_str[32], filename[256];
	time_t time_now;
	struct tm *tm_time_now;
	struct tm time_buff;
	FILE *file_ptr;
	int fd, written_bytes;
	BOOL ret_val =  FALSE;
	
	/* get the proper file name */
	time(&time_now);
	tm_time_now = localtime_r(&time_now, &time_buff);
	strftime(time_str, 32, "%m%d", tm_time_now);
	snprintf(filename, 256, "%s%s.%s", g_file_name, time_str, g_file_postfix);
	filename[sizeof(filename) - 1] = '\0';	
	if (NULL == (file_ptr = fopen(filename, "a+"))) {
		printf("[log_plugin]: failed to create log file %s: %s\n",
		       filename, strerror(errno));
	} else {
		fd = fileno(file_ptr);
		written_bytes = write(fd, g_log_buf_ptr, g_current_size);
		if (written_bytes != g_current_size) {
			printf("[log_plugin]: fail to write buffer into file %s\n",
					filename);
		} else {
			ret_val = TRUE;
		}
		fclose(file_ptr);
	}
	g_current_size = 0;
	return ret_val;

}

