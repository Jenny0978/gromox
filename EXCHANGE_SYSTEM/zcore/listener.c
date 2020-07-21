#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libHX/defs.h>
#include <gromox/defs.h>
#include "listener.h"
#include "rpc_parser.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static BOOL g_notify_stop;
static int g_listen_sockd;
static pthread_t g_listener_id;

static void* thread_work_func(void *param)
{
    struct sockaddr_un unix_addr;
	
	while (FALSE == g_notify_stop) {
		socklen_t len = sizeof(unix_addr);
		memset(&unix_addr, 0, sizeof(unix_addr));
		int clifd = accept(g_listen_sockd, reinterpret_cast(struct sockaddr *, &unix_addr), &len);
		if (-1 == clifd) {
			continue;
		}
		if (FALSE == rpc_parser_activate_connection(clifd)) {
			close(clifd);
		}
    }
	return NULL;
}

void listener_init()
{
	g_listen_sockd = -1;
	g_notify_stop = TRUE;
}

int listener_run(const char *CS_PATH)
{
	int len;
	struct sockaddr_un unix_addr;

	 /* Create a Unix domain stream socket */
	g_listen_sockd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (-1 == g_listen_sockd) {
		printf("[listener]: failed to create listen socket: %s\n", strerror(errno));
		return -1;
	}
	unlink(CS_PATH);
	/* Fill in socket address structure */
	memset(&unix_addr, 0, sizeof (unix_addr));
	unix_addr.sun_family = AF_UNIX;
	strcpy(unix_addr.sun_path, CS_PATH);
	len = sizeof(unix_addr.sun_family) + strlen(unix_addr.sun_path);
	/* Bind the name to the descriptor */
	if (bind(g_listen_sockd, (struct sockaddr*)&unix_addr, len) < 0) {
		close(g_listen_sockd);
		printf("[listener]: bind %s: %s\n", unix_addr.sun_path, strerror(errno));
		return -2;
	}
	if (chmod(CS_PATH, 0666) < 0) {
		close(g_listen_sockd);
		printf("[listener]: fail to change access mode of %s\n", CS_PATH);
		return -3;
	}
	if (listen(g_listen_sockd, 5) < 0) {
		close(g_listen_sockd);
		printf("[listener]: fail to listen!\n");
		return -4;
	}
	g_notify_stop = FALSE;
	int ret = pthread_create(&g_listener_id, nullptr, thread_work_func, nullptr);
	if (ret != 0) {
		close(g_listen_sockd);
		printf("[listener]: failed to create accept thread: %s\n", strerror(ret));
		return -5;
	}
	pthread_setname_np(g_listener_id, "accept");
	return 0;
}

int listener_stop()
{
	g_notify_stop = TRUE;
	shutdown(g_listen_sockd, SHUT_RDWR);
	pthread_join(g_listener_id, NULL);
	close(g_listen_sockd);
	g_listen_sockd = -1;
	return 0;
}

void listener_free()
{
	/* do nothing */
}
