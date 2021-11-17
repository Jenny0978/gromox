// SPDX-License-Identifier: GPL-2.0-only WITH linking exception
#include <cerrno>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include "ftstream_parser.h"
#include "rop_processor.h"
#include <gromox/defs.h>
#include <gromox/endian.hpp>
#include <gromox/mapidefs.h>
#include <gromox/proc_common.h>
#include "common_util.h"
#include <gromox/util.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>

using namespace std::string_literals;

enum {
	FTSTREAM_PARSER_READ_FAIL = -1,
	FTSTREAM_PARSER_READ_OK,
	FTSTREAM_PARSER_READ_CONTINUE
};


static BOOL ftstream_parser_read_uint16(
	FTSTREAM_PARSER *pstream, uint16_t *pv)
{
	if (read(pstream->fd, pv, sizeof(*pv)) != sizeof(*pv))
		return FALSE;
	*pv = le16_to_cpu(*pv);
	pstream->offset += sizeof(uint16_t);
	return TRUE;
}

static BOOL ftstream_parser_read_uint32(
	FTSTREAM_PARSER *pstream, uint32_t *pv)
{
	if (read(pstream->fd, pv, sizeof(*pv)) != sizeof(*pv))
		return FALSE;
	*pv = le32_to_cpu(*pv);
	pstream->offset += sizeof(uint32_t);
	return TRUE;
}

static BOOL ftstream_parser_read_uint64(
	FTSTREAM_PARSER *pstream, uint64_t *pv)
{
	if (read(pstream->fd, pv, sizeof(*pv)) != sizeof(*pv))
		return FALSE;
	*pv = le64_to_cpu(*pv);
	pstream->offset += sizeof(uint64_t);
	return TRUE;
}

static char* ftstream_parser_read_wstring(
	FTSTREAM_PARSER *pstream, BOOL *pb_continue)
{
	char *pbuff;
	char *pbuff1;
	uint32_t len;
	uint32_t tmp_len;
	uint32_t origin_offset;
	
	*pb_continue = FALSE;
	origin_offset = pstream->offset;
	if (FALSE == ftstream_parser_read_uint32(
		pstream, &len)) {
		return NULL;
	}
	if (len >= common_util_get_param(
		COMMON_UTIL_MAX_MAIL_LENGTH)) {
		return NULL;	
	}
	if (origin_offset + sizeof(uint32_t) + len >
		pstream->st_size) {
		*pb_continue = TRUE;
		return NULL;
	}
	tmp_len = 2*len;
	pbuff = me_alloc<char>(len + 2);
	if (NULL == pbuff) {
		return NULL;
	}
	if (len != read(pstream->fd, pbuff, len)) {
		free(pbuff);
		return NULL;
	}
	pstream->offset += len;
	/* if trail nulls not found, append them */
	if (0 != pbuff[len - 2] && 0 != pbuff[len - 1]) {
		pbuff[len] = 0;
		pbuff[len + 1] = 0;
		len += 2;
	}
	pbuff1 = cu_alloc<char>(tmp_len);
	if (NULL == pbuff1) {
		free(pbuff);
		return NULL;
	}
	if (FALSE == utf16le_to_utf8(
		pbuff, len, pbuff1, tmp_len)) {
		free(pbuff);
		return NULL;
	}
	free(pbuff);
	return pbuff1;
}

static char* ftstream_parser_read_string(
	FTSTREAM_PARSER *pstream, BOOL *pb_continue)
{
	char *pbuff;
	uint32_t len;
	uint32_t origin_offset;
	
	*pb_continue = FALSE;
	origin_offset = pstream->offset;
	if (FALSE == ftstream_parser_read_uint32(
		pstream, &len)) {
		return NULL;
	}
	if (len >= common_util_get_param(
		COMMON_UTIL_MAX_MAIL_LENGTH)) {
		return nullptr;
	}
	if (origin_offset + sizeof(uint32_t) + len >
		pstream->st_size) {
		*pb_continue = TRUE;
		return NULL;
	}
	pbuff = cu_alloc<char>(len + 1);
	if (NULL == pbuff) {
		return NULL;
	}
	if (len != read(pstream->fd, pbuff, len)) {
		return NULL;
	}
	pstream->offset += len;
	/* if trail null not found, append it */
	if ('\0' != pbuff[len - 1]) {
		pbuff[len] = '\0';
	}
	return pbuff;
}

static char* ftstream_parser_read_naked_wstring(
	FTSTREAM_PARSER *pstream)
{
	char *pbuff;
	uint32_t len;
	char buff[1024];
	uint32_t offset;
	
	offset = 0;
	while (true) {
		if (2 != read(pstream->fd, buff + offset, 2)) {
			return NULL;
		}
		if (0 == buff[offset] && 0 == buff[offset + 1]) {
			break;
		}
		offset += 2;
		if (offset == sizeof(buff)) {
			return NULL;
		}
	}
	len = offset + 2;
	pstream->offset += len;
	pbuff = cu_alloc<char>(2 * len);
	if (NULL == pbuff) {
		return NULL;
	}
	if (FALSE == utf16le_to_utf8(
		buff, len, pbuff, 2*len)) {
		return NULL;
	}
	return pbuff;
}

static BOOL ftstream_parser_read_guid(
	FTSTREAM_PARSER *pstream, GUID *pguid)
{
	if (FALSE == ftstream_parser_read_uint32(
		pstream, &pguid->time_low)) {
		return FALSE;
	}
	if (FALSE == ftstream_parser_read_uint16(
		pstream, &pguid->time_mid)) {
		return FALSE;
	}
	if (FALSE == ftstream_parser_read_uint16(
		pstream, &pguid->time_hi_and_version)) {
		return FALSE;
	}
	if (2 != read(pstream->fd, pguid->clock_seq, 2)) {
		return FALSE;
	}
	pstream->offset += 2;
	if (6 != read(pstream->fd, pguid->node, 6)) {
		return FALSE;
	}
	pstream->offset += 6;
	return TRUE;
}

static BOOL ftstream_parser_read_svreid(
	FTSTREAM_PARSER *pstream,
	SVREID *psvreid, BOOL *pb_continue)
{
	uint32_t len;
	uint8_t ours;
	uint32_t origin_offset;
	
	*pb_continue = FALSE;
	origin_offset = pstream->offset;
	if (FALSE == ftstream_parser_read_uint32(
		pstream, &len)) {
		return FALSE;
	}
	if (origin_offset + sizeof(uint32_t) + len >
		pstream->st_size) {
		*pb_continue = TRUE;
		return FALSE;
	}
	if (sizeof(uint8_t) != read(pstream->fd,
		&ours, sizeof(uint8_t))) {
		return FALSE;
	}
	pstream->offset += sizeof(uint8_t);
	if (0 == ours) {
		psvreid->pbin = cu_alloc<BINARY>();
		if (NULL == psvreid->pbin) {
			return FALSE;
		}
		psvreid->pbin->cb = len - 1;
		if (0 == psvreid->pbin->cb) {
			psvreid->pbin->pb = NULL;
		} else {
			psvreid->pbin->pv = common_util_alloc(psvreid->pbin->cb);
			if (psvreid->pbin->pv == nullptr)
				return FALSE;
			if (read(pstream->fd, psvreid->pbin->pv, psvreid->pbin->cb) != psvreid->pbin->cb)
				return FALSE;
			pstream->offset += psvreid->pbin->cb;
		}
	}
	if (21 != len) {
		return FALSE;
	}
	psvreid->pbin = NULL;
	if (FALSE == ftstream_parser_read_uint64(
		pstream, &psvreid->folder_id)) {
		return FALSE;
	}
	if (FALSE == ftstream_parser_read_uint64(
		pstream, &psvreid->message_id)) {
		return FALSE;
	}
	if (FALSE == ftstream_parser_read_uint32(
		pstream, &psvreid->instance)) {
		return FALSE;
	}
	return TRUE;
}

static BOOL ftstream_parser_read_binary(
	FTSTREAM_PARSER *pstream, BINARY *pbin,
	BOOL *pb_continue)
{
	uint32_t origin_offset;
	
	*pb_continue = FALSE;
	origin_offset = pstream->offset;
	if (FALSE == ftstream_parser_read_uint32(
		pstream, &pbin->cb)) {
		return FALSE;
	}
	if (pbin->cb >= common_util_get_param(
		COMMON_UTIL_MAX_MAIL_LENGTH)) {
		return FALSE;	
	}
	if (origin_offset + sizeof(uint32_t) +
		pbin->cb > pstream->st_size) {
		*pb_continue = TRUE;
		return FALSE;
	}
	if (0 == pbin->cb) {
		pbin->pb = NULL;
		return TRUE;
	}
	pbin->pv = common_util_alloc(pbin->cb);
	if (pbin->pv == nullptr)
		return FALSE;
	if (read(pstream->fd, pbin->pv, pbin->cb) != pbin->cb)
		return FALSE;
	pstream->offset += pbin->cb;
	return TRUE;
}

static PROPERTY_NAME* ftstream_parser_read_property_name(
	FTSTREAM_PARSER *pstream)
{
	auto pname = cu_alloc<PROPERTY_NAME>();
	if (NULL == pname) {
		return NULL;
	}
	if (FALSE == ftstream_parser_read_guid(
		pstream, &pname->guid)) {
		return NULL;	
	}
	if (sizeof(uint8_t) != read(pstream->fd,
		&pname->kind, sizeof(uint8_t))) {
		return NULL;
	}
	pstream->offset += sizeof(uint8_t);
	pname->lid = 0;
	pname->pname = NULL;
	switch (pname->kind) {
	case MNID_ID:
		if (!ftstream_parser_read_uint32(pstream, &pname->lid))
			return nullptr;
		return pname;
	case MNID_STRING:
		pname->pname = ftstream_parser_read_naked_wstring(pstream);
		if (NULL == pname->pname) {
			return NULL;
		}
		return pname;
	}
	return NULL;
}

static int ftstream_parser_read_element(FTSTREAM_PARSER &stream,
    uint32_t &marker, TAGGED_PROPVAL &propval)
{
	auto pstream = &stream;
	uint32_t count;
	BOOL b_continue;
	uint32_t atom_element = 0;
	PROPERTY_NAME *ppropname;
	
	uint32_t origin_offset = pstream->offset;
	if (origin_offset == pstream->st_size) {
		return FTSTREAM_PARSER_READ_CONTINUE;
	}
	if (FALSE == ftstream_parser_read_uint32(
		pstream, &atom_element)) {
		return FTSTREAM_PARSER_READ_FAIL;
	}
	switch (atom_element) {
	case STARTTOPFLD:
	case STARTSUBFLD:
	case ENDFOLDER:
	case STARTMESSAGE:
	case ENDMESSAGE:
	case STARTFAIMSG:
	case STARTEMBED:
	case ENDEMBED:
	case STARTRECIP:
	case ENDTORECIP:
	case NEWATTACH:
	case ENDATTACH:
	case INCRSYNCCHG:
	case INCRSYNCCHGPARTIAL:
	case INCRSYNCDEL:
	case INCRSYNCEND:
	case INCRSYNCREAD:
	case INCRSYNCSTATEBEGIN:
	case INCRSYNCSTATEEND:
	case INCRSYNCPROGRESSMODE:
	case INCRSYNCPROGRESSPERMSG:
	case INCRSYNCMESSAGE:
	case INCRSYNCGROUPINFO:
	case FXERRORINFO:
		marker = atom_element;
		return FTSTREAM_PARSER_READ_OK;
	}
	marker = 0;
	uint16_t proptype = PROP_TYPE(atom_element);
	uint16_t propid = PROP_ID(atom_element);
	/* META_TAG_IDSETGIVEN, MS-OXCFXICS 3.2.5.2.1 */
	if (META_TAG_IDSETGIVEN == atom_element) {
		proptype = PT_BINARY;
	}
	if (propid == PROP_ID_INVALID)
		fprintf(stderr, "W-1272: ftstream with PROP_ID_INVALID seen\n");
	if (is_nameprop_id(propid)) {
		ppropname = ftstream_parser_read_property_name(pstream);
		if (NULL == ppropname) {
			return FTSTREAM_PARSER_READ_FAIL;
		}
		if (!pstream->plogon->get_named_propid(TRUE, ppropname, &propid))
			return FTSTREAM_PARSER_READ_FAIL;
	}
	if (pstream->st_size == pstream->offset) {
		goto CONTINUE_WAITING;
	}
	propval.proptag = PROP_TAG(proptype, propid);
	if (proptype & FXICS_CODEPAGE_FLAG) {
		/* codepage string */
		auto codepage = proptype & ~FXICS_CODEPAGE_FLAG;
		if (1200 == codepage) {
			propval.proptag = CHANGE_PROP_TYPE(propval.proptag, PT_UNICODE);
			propval.pvalue = ftstream_parser_read_wstring(pstream, &b_continue);
		} else {
			propval.pvalue = ftstream_parser_read_string(pstream, &b_continue);
		}
		if (propval.pvalue == nullptr) {
			if (b_continue)
				goto CONTINUE_WAITING;
			return FTSTREAM_PARSER_READ_FAIL;
		}
		return FTSTREAM_PARSER_READ_OK;
	}
	switch (proptype) {
	case PT_SHORT: {
		auto v = cu_alloc<uint16_t>();
		if (v == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		propval.pvalue = v;
		return ftstream_parser_read_uint16(pstream, v) ? FTSTREAM_PARSER_READ_OK : FTSTREAM_PARSER_READ_FAIL;
	}
	case PT_ERROR:
	case PT_LONG: {
		auto v = cu_alloc<uint32_t>();
		if (v == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		propval.pvalue = v;
		return ftstream_parser_read_uint32(pstream, v) ? FTSTREAM_PARSER_READ_OK : FTSTREAM_PARSER_READ_FAIL;
	}
	case PT_FLOAT: {
		auto v = cu_alloc<float>();
		if (v == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		propval.pvalue = v;
		if (read(pstream->fd, v, sizeof(*v)) != sizeof(*v))
			return FTSTREAM_PARSER_READ_FAIL;	
		pstream->offset += sizeof(*v);
		return FTSTREAM_PARSER_READ_OK;
	}
	case PT_DOUBLE:
	case PT_APPTIME: {
		auto v = cu_alloc<double>();
		if (v == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		propval.pvalue = v;
		if (read(pstream->fd, v, sizeof(*v)) != sizeof(*v))
			return FTSTREAM_PARSER_READ_FAIL;	
		pstream->offset += sizeof(*v);
		return FTSTREAM_PARSER_READ_OK;
	}
	case PT_BOOLEAN: {
		auto v = cu_alloc<uint8_t>();
		if (v == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		propval.pvalue = v;
		uint16_t fake_byte = 0;
		if (FALSE == ftstream_parser_read_uint16(
			pstream, &fake_byte)) {
			return FTSTREAM_PARSER_READ_FAIL;	
		}
		*v = fake_byte;
		return FTSTREAM_PARSER_READ_OK;
	}
	case PT_CURRENCY:
	case PT_I8:
	case PT_SYSTIME: {
		auto v = cu_alloc<uint64_t>();
		if (v == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		propval.pvalue = v;
		return ftstream_parser_read_uint64(pstream, v) ? FTSTREAM_PARSER_READ_OK : FTSTREAM_PARSER_READ_FAIL;
	}
	case PT_STRING8:
		propval.pvalue = ftstream_parser_read_string(pstream, &b_continue);
		if (propval.pvalue == nullptr) {
			if (b_continue)
				goto CONTINUE_WAITING;
			return FTSTREAM_PARSER_READ_FAIL;
		}
		return FTSTREAM_PARSER_READ_OK;
	case PT_UNICODE:
		propval.pvalue = ftstream_parser_read_wstring(pstream, &b_continue);
		if (propval.pvalue == nullptr) {
			if (b_continue)
				goto CONTINUE_WAITING;
			return FTSTREAM_PARSER_READ_FAIL;
		}
		return FTSTREAM_PARSER_READ_OK;
	case PT_CLSID: {
		auto v = cu_alloc<GUID>();
		if (v == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		propval.pvalue = v;
		return ftstream_parser_read_guid(pstream, v) ? FTSTREAM_PARSER_READ_OK : FTSTREAM_PARSER_READ_FAIL;
	}
	case PT_SVREID: {
		auto v = cu_alloc<SVREID>();
		if (v == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		propval.pvalue = v;
		if (ftstream_parser_read_svreid(pstream, v, &b_continue))
			return FTSTREAM_PARSER_READ_OK;
		if (b_continue)
			goto CONTINUE_WAITING;
		return FTSTREAM_PARSER_READ_FAIL;
	}
	case PT_OBJECT:
	case PT_BINARY: {
		auto v = cu_alloc<BINARY>();
		if (v == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		propval.pvalue = v;
		if (ftstream_parser_read_binary(pstream, v, &b_continue))
			return FTSTREAM_PARSER_READ_OK;
		if (b_continue)
			goto CONTINUE_WAITING;
		return FTSTREAM_PARSER_READ_FAIL;
	}
	case PT_MV_SHORT: {
		auto sa = cu_alloc<SHORT_ARRAY>();
		propval.pvalue = sa;
		if (sa == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		if (FALSE == ftstream_parser_read_uint32(
			pstream, &count)) {
			return FTSTREAM_PARSER_READ_FAIL;
		}
		if (count*sizeof(uint16_t) > 0x10000) {
			return FTSTREAM_PARSER_READ_FAIL;
		}
		if (pstream->st_size < count*sizeof(uint16_t) +
			pstream->offset) {
			goto CONTINUE_WAITING;
		}
		sa->count = count;
		if (0 == count) {
			sa->ps = nullptr;
		} else {
			sa->ps = cu_alloc<uint16_t>(count);
			if (sa->ps == nullptr)
				return FTSTREAM_PARSER_READ_FAIL;
		}
		for (size_t i = 0; i < count; ++i) {
			if (!ftstream_parser_read_uint16(pstream, &sa->ps[i]))
				return FTSTREAM_PARSER_READ_FAIL;	
		}
		return FTSTREAM_PARSER_READ_OK;
	}
	case PT_MV_LONG: {
		auto la = cu_alloc<LONG_ARRAY>();
		propval.pvalue = la;
		if (la == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		if (FALSE == ftstream_parser_read_uint32(
			pstream, &count)) {
			return FTSTREAM_PARSER_READ_FAIL;
		}
		if (count*sizeof(uint32_t) > 0x10000) {
			return FTSTREAM_PARSER_READ_FAIL;
		}
		if (pstream->st_size < count*sizeof(uint32_t) +
			pstream->offset) {
			goto CONTINUE_WAITING;
		}
		la->count = count;
		if (0 == count) {
			la->pl = nullptr;
		} else {
			la->pl = cu_alloc<uint32_t>(count);
			if (la->pl == nullptr)
				return FTSTREAM_PARSER_READ_FAIL;
		}
		for (size_t i = 0; i < count; ++i) {
			if (!ftstream_parser_read_uint32(pstream, &la->pl[i]))
				return FTSTREAM_PARSER_READ_FAIL;	
		}
		return FTSTREAM_PARSER_READ_OK;
	}
	case PT_MV_CURRENCY:
	case PT_MV_I8:
	case PT_MV_SYSTIME: {
		auto la = cu_alloc<LONGLONG_ARRAY>();
		propval.pvalue = la;
		if (la == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		if (FALSE == ftstream_parser_read_uint32(
			pstream, &count)) {
			return FTSTREAM_PARSER_READ_FAIL;
		}
		if (count*sizeof(uint64_t) > 0x10000) {
			return FTSTREAM_PARSER_READ_FAIL;
		}
		if (pstream->st_size < count*sizeof(uint64_t) +
			pstream->offset) {
			goto CONTINUE_WAITING;
		}
		la->count = count;
		if (0 == count) {
			la->pll = nullptr;
		} else {
			la->pll = cu_alloc<uint64_t>(count);
			if (la->pll == nullptr)
				return FTSTREAM_PARSER_READ_FAIL;
		}
		for (size_t i = 0; i < count; ++i) {
			if (!ftstream_parser_read_uint64(pstream, &la->pll[i]))
				return FTSTREAM_PARSER_READ_FAIL;	
		}
		return FTSTREAM_PARSER_READ_OK;
	}
	case PT_MV_STRING8: {
		auto sa = cu_alloc<STRING_ARRAY>();
		propval.pvalue = sa;
		if (sa == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		if (FALSE == ftstream_parser_read_uint32(
			pstream, &count)) {
			return FTSTREAM_PARSER_READ_FAIL;
		}
		if (pstream->st_size == pstream->offset) {
			goto CONTINUE_WAITING;
		}
		sa->count = count;
		if (0 == count) {
			sa->ppstr = nullptr;
		} else {
			sa->ppstr = cu_alloc<char *>(count);
			if (sa->ppstr == nullptr)
				return FTSTREAM_PARSER_READ_FAIL;
		}
		for (size_t i = 0; i < count; ++i) {
			sa->ppstr[i] = ftstream_parser_read_string(pstream, &b_continue);
			if (sa->ppstr[i] == nullptr) {
				if (!b_continue)
					return FTSTREAM_PARSER_READ_FAIL;
				if (pstream->offset - origin_offset > 0x10000) {
					return FTSTREAM_PARSER_READ_FAIL;
				}
				goto CONTINUE_WAITING;
			}
			if (pstream->st_size == pstream->offset) {
				if (pstream->offset - origin_offset > 0x10000) {
					return FTSTREAM_PARSER_READ_FAIL;
				}
				goto CONTINUE_WAITING;
			}
		}
		return FTSTREAM_PARSER_READ_OK;
	}
	case PT_MV_UNICODE: {
		auto sa = cu_alloc<STRING_ARRAY>();
		propval.pvalue = sa;
		if (sa == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		if (FALSE == ftstream_parser_read_uint32(
			pstream, &count)) {
			return FTSTREAM_PARSER_READ_FAIL;
		}
		if (pstream->st_size == pstream->offset) {
			goto CONTINUE_WAITING;
		}
		sa->count = count;
		if (0 == count) {
			sa->ppstr = nullptr;
		} else {
			sa->ppstr = cu_alloc<char *>(count);
			if (sa->ppstr == nullptr)
				return FTSTREAM_PARSER_READ_FAIL;
		}
		for (size_t i = 0; i < count; ++i) {
			sa->ppstr[i] = ftstream_parser_read_wstring(pstream, &b_continue);
			if (sa->ppstr[i] == nullptr) {
				if (!b_continue)
					return FTSTREAM_PARSER_READ_FAIL;
				if (pstream->offset - origin_offset > 0x10000) {
					return FTSTREAM_PARSER_READ_FAIL;
				}
				goto CONTINUE_WAITING;
			}
			if (pstream->st_size == pstream->offset) {
				if (pstream->offset - origin_offset > 0x10000) {
					return FTSTREAM_PARSER_READ_FAIL;
				}
				goto CONTINUE_WAITING;
			}
		}
		return FTSTREAM_PARSER_READ_OK;
	}
	case PT_MV_CLSID: {
		auto ga = cu_alloc<GUID_ARRAY>();
		propval.pvalue = ga;
		if (ga == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		if (FALSE == ftstream_parser_read_uint32(
			pstream, &count)) {
			return FTSTREAM_PARSER_READ_FAIL;
		}
		if (16*count > 0x10000) {
			return FTSTREAM_PARSER_READ_FAIL;
		}
		if (pstream->st_size < 16*count + pstream->offset) {
			goto CONTINUE_WAITING;
		}
		ga->count = count;
		if (0 == count) {
			ga->pguid = nullptr;
		} else {
			ga->pguid = cu_alloc<GUID>(count);
			if (ga->pguid == nullptr)
				return FTSTREAM_PARSER_READ_FAIL;
		}
		for (size_t i = 0; i < count; ++i) {
			if (!ftstream_parser_read_guid(pstream, &ga->pguid[i]))
				return FTSTREAM_PARSER_READ_FAIL;	
		}
		return FTSTREAM_PARSER_READ_OK;
	}
	case PT_MV_BINARY: {
		auto ba = cu_alloc<BINARY_ARRAY>();
		propval.pvalue = ba;
		if (ba == nullptr)
			return FTSTREAM_PARSER_READ_FAIL;
		if (FALSE == ftstream_parser_read_uint32(
			pstream, &count)) {
			return FTSTREAM_PARSER_READ_FAIL;
		}
		if (pstream->st_size == pstream->offset) {
			goto CONTINUE_WAITING;
		}
		ba->count = count;
		if (0 == count) {
			ba->pbin = nullptr;
		} else {
			ba->pbin = cu_alloc<BINARY>(ba->count);
			if (ba->pbin == nullptr) {
				ba->count = 0;
				return FTSTREAM_PARSER_READ_FAIL;
			}
		}
		for (size_t i = 0; i < count; ++i) {
			if (!ftstream_parser_read_binary(pstream,
			    ba->pbin + i, &b_continue)) {
				if (!b_continue)
					return FTSTREAM_PARSER_READ_FAIL;
				if (pstream->offset - origin_offset > 0x10000) {
					return FTSTREAM_PARSER_READ_FAIL;
				}
				goto CONTINUE_WAITING;
			}
			if (pstream->st_size == pstream->offset) {
				if (pstream->offset - origin_offset > 0x10000) {
					return FTSTREAM_PARSER_READ_FAIL;
				}
				goto CONTINUE_WAITING;
			}
		}
		return FTSTREAM_PARSER_READ_OK;
	}
	}
	return FTSTREAM_PARSER_READ_FAIL;
	
 CONTINUE_WAITING:
	pstream->offset = origin_offset;
	return FTSTREAM_PARSER_READ_CONTINUE;
}

BOOL FTSTREAM_PARSER::write_buffer(const BINARY *ptransfer_data)
{
	auto pstream = this;
	lseek(pstream->fd, 0, SEEK_END);
	if (ptransfer_data->cb != write(pstream->fd,
		ptransfer_data->pb, ptransfer_data->cb)) {
		return FALSE;	
	}
	pstream->st_size += ptransfer_data->cb;
	return TRUE;
}

static BOOL ftstream_parser_truncate_fd(
	FTSTREAM_PARSER *pstream)
{
	if (0 == pstream->offset) {
		return TRUE;
	}
	if (pstream->st_size == pstream->offset) {
		ftruncate(pstream->fd, 0);
		lseek(pstream->fd, 0, SEEK_SET);
		pstream->st_size = 0;
		pstream->offset = 0;
		return TRUE;
	}
	if (lseek(pstream->fd, pstream->offset, SEEK_SET) < 0)
		fprintf(stderr, "W-1425: lseek: %s\n", strerror(errno));
	char buff[0x10000];
	auto len = read(pstream->fd, buff, sizeof(buff));
	if (len <= 0) {
		return FALSE;
	}
	ftruncate(pstream->fd, 0);
	lseek(pstream->fd, 0, SEEK_SET);
	if (len != write(pstream->fd, buff, len)) {
		return FALSE;
	}
	pstream->st_size = len;
	pstream->offset = 0;
	return TRUE;
}

gxerr_t FTSTREAM_PARSER::process(RECORD_MARKER record_marker,
    RECORD_PROPVAL record_propval, void *pparam)
{
	auto pstream = this;
	uint32_t marker;
	TAGGED_PROPVAL propval{};
	
	lseek(pstream->fd, 0, SEEK_SET);
	pstream->offset = 0;
	while (true) {
		switch (ftstream_parser_read_element(*this, marker, propval)) {
		case FTSTREAM_PARSER_READ_OK: {
			if (0 != marker) {
				gxerr_t err = record_marker(static_cast<fastupctx_object *>(pparam), marker);
				if (err != GXERR_SUCCESS)
					return err;
				break;
			}
			auto proptype = PROP_TYPE(propval.proptag);
			if (proptype & FXICS_CODEPAGE_FLAG) {
				auto codepage = proptype & ~FXICS_CODEPAGE_FLAG;
				auto len = 2 * strlen(static_cast<char *>(propval.pvalue)) + 2;
				auto pvalue = common_util_alloc(len);
				if (pvalue == nullptr || common_util_mb_to_utf8(codepage,
				    static_cast<char *>(propval.pvalue),
				    static_cast<char *>(pvalue), len) <= 0) {
					propval.proptag = CHANGE_PROP_TYPE(propval.proptag, PT_STRING8);
				} else {
					propval.proptag = CHANGE_PROP_TYPE(propval.proptag, PT_UNICODE);
					propval.pvalue = pvalue;
				}
			}
			gxerr_t err = record_propval(static_cast<fastupctx_object *>(pparam), &propval);
			if (err != GXERR_SUCCESS)
				return err;
			break;
		}
		case FTSTREAM_PARSER_READ_CONTINUE:
			return ftstream_parser_truncate_fd(pstream) == TRUE ?
			       GXERR_SUCCESS : GXERR_CALL_FAILED;
		default:
			return GXERR_CALL_FAILED;
		}
	}
}

std::unique_ptr<ftstream_parser> ftstream_parser::create(logon_object *plogon) try
{
	auto stream_id = common_util_get_ftstream_id();
	auto rpc_info = get_rpc_info();
	auto path = rpc_info.maildir + "/tmp/faststream"s;
	if (mkdir(path.c_str(), 0777) < 0 && errno != EEXIST) {
		fprintf(stderr, "E-1428: mkdir %s: %s\n", path.c_str(), strerror(errno));
		return nullptr;
	}
	std::unique_ptr<ftstream_parser> pstream(new ftstream_parser);
	pstream->offset = 0;
	pstream->st_size = 0;
	pstream->path = std::move(path) + "/" + std::to_string(stream_id) + "." + get_host_ID();
	pstream->fd = open(pstream->path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0666);
	if (-1 == pstream->fd) {
		fprintf(stderr, "E-1429: open %s: %s\n", pstream->path.c_str(), strerror(errno));
		return NULL;
	}
	pstream->plogon = plogon;
	return pstream;
} catch (const std::bad_alloc &) {
	fprintf(stderr, "E-1450: ENOMEM\n");
	return nullptr;
}

FTSTREAM_PARSER::~FTSTREAM_PARSER()
{
	auto pstream = this;
	close(pstream->fd);
	if (remove(pstream->path.c_str()) < 0 && errno != ENOENT)
		fprintf(stderr, "W-1392: remove %s: %s\n", pstream->path.c_str(), strerror(errno));
}