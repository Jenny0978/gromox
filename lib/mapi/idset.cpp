// SPDX-License-Identifier: GPL-2.0-only WITH linking exception
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>
#include <gromox/endian.hpp>
#include <gromox/mapi_types.hpp>
#include <gromox/util.hpp>
#include <gromox/rop_util.hpp>
#include <cstdlib>
#include <cstring>

namespace {
struct STACK_NODE {
	STACK_NODE(const uint8_t *b, uint8_t l) noexcept : common_length(l)
		{ memcpy(pcommon_bytes, b, common_length); }
	uint8_t common_length;
	uint8_t pcommon_bytes[6];
};
using byte_stack = std::vector<STACK_NODE>;

struct mdel {
	inline void operator()(BINARY *x) const { rop_util_free_binary(x); }
};
}

idset::idset(bool ser, uint8_t type) :
	b_serialize(ser), repl_type(type)
{}

std::unique_ptr<idset> idset::create(bool ser, uint8_t type) try
{
	return std::make_unique<idset>(ser, type);
} catch (const std::bad_alloc &) {
	return nullptr;
}

BOOL idset::register_mapping(BINARY *pparam, REPLICA_MAPPING mapping)
{
	auto pset = this;
	if (NULL != pset->pparam ||
		NULL != pset->mapping) {
		return FALSE;
	}
	if (NULL == pparam) {
		pset->pparam = NULL;
	} else if (pparam->cb == 0) {
		pset->pparam = NULL;
	} else {
		pset->pparam = malloc(pparam->cb);
		if (NULL == pset->pparam) {
			return FALSE;
		}
		memcpy(pset->pparam, pparam->pb, pparam->cb);
	}
	pset->mapping = mapping;
	return TRUE;
}

idset::~idset()
{
	auto pset = this;
	if (NULL != pset->pparam) {
		free(pset->pparam);
	}
}

BOOL idset::check_empty() const
{
	return repl_list.empty() ? TRUE : false;
}

static BOOL idset_append_internal(IDSET *pset,
    uint16_t replid, uint64_t value) try
{
	if (!pset->b_serialize)
		return FALSE;
	auto prepl_node = std::find_if(pset->repl_list.begin(), pset->repl_list.end(),
	                  [&](const repl_node &n) { return n.replid == replid; });
	if (prepl_node == pset->repl_list.end())
		prepl_node = pset->repl_list.emplace(pset->repl_list.end(), replid);

	auto &range_list = prepl_node->range_list;
	auto prange_node = range_list.begin();
	for (; prange_node != range_list.end(); ++prange_node) {
		if (value >= prange_node->low_value &&
			value <= prange_node->high_value) {
			return TRUE;
		} else if (value == prange_node->low_value - 1) {
			prange_node->low_value = value;
			if (prange_node == range_list.begin())
				return TRUE;
			auto prange_node1 = std::prev(prange_node);
			if (prange_node1->high_value < prange_node->low_value)
				return TRUE;
			prange_node->low_value = prange_node1->low_value;
			range_list.erase(prange_node1);
			return TRUE;
		} else if (value == prange_node->high_value + 1) {
			prange_node->high_value = value;
			auto prange_node1 = std::next(prange_node);
			if (prange_node1 == range_list.end())
				return TRUE;
			if (prange_node1->low_value > prange_node->high_value)
				return TRUE;
			prange_node->high_value = prange_node1->high_value;
			range_list.erase(prange_node1);
			return TRUE;
		} else if (prange_node->low_value > value) {
			break;
		}
	}
	if (prange_node != range_list.end())
		range_list.emplace(prange_node, value, value);
	else
		range_list.emplace_back(value, value);
	return TRUE;
} catch (const std::bad_alloc &) {
	fprintf(stderr, "E-1613: ENOMEM\n");
	return false;
}

BOOL idset::append(uint64_t eid)
{
	return idset_append_internal(this, rop_util_get_replid(eid),
	       rop_util_get_gc_value(eid));
}

BOOL idset::append_range(uint16_t replid,
    uint64_t low_value, uint64_t high_value) try
{
	auto pset = this;
	
	if (!pset->b_serialize)
		return FALSE;
	if (low_value > high_value) {
		return FALSE;
	}
	auto prepl_node = std::find_if(repl_list.begin(), repl_list.end(),
	                  [&](const repl_node &n) { return n.replid == replid; });
	if (prepl_node == repl_list.end())
		prepl_node = repl_list.emplace(repl_list.end(), replid);
	auto &range_list = prepl_node->range_list;
	auto prange_node1 = range_list.end();
	for (auto prange_node = range_list.begin();
	     prange_node != range_list.end(); ++prange_node) {
		if (prange_node1 == range_list.end()) {
			auto succ = std::next(prange_node);
			if (low_value == prange_node->high_value) {
				prange_node1 = prange_node;
				prange_node1->high_value = high_value;
			} else if (low_value > prange_node->high_value &&
			    (succ == range_list.end() || high_value <= succ->low_value)) {
				prange_node = prange_node1 = range_list.emplace(prange_node, low_value, high_value);
			}
			continue;
		}
		if (high_value == prange_node->low_value) {
			prange_node1->high_value = prange_node->high_value;
			range_list.erase(prange_node);
			return TRUE;
		} else if (high_value < prange_node->low_value) {
			return TRUE;
		}
		prange_node = range_list.erase(prange_node);
		if (prange_node == range_list.end())
			return TRUE;
	}
	if (prange_node1 != range_list.end())
		return TRUE;
	range_list.emplace_back(low_value, high_value);
	return TRUE;
} catch (const std::bad_alloc &) {
	fprintf(stderr, "E-1614: ENOMEM\n");
	return false;
}

void idset::remove(uint64_t eid) try
{
	auto pset = this;
	
	if (!pset->b_serialize)
		return;
	auto replid = rop_util_get_replid(eid);
	auto value = rop_util_get_gc_value(eid);
	auto prepl_node = std::find_if(repl_list.begin(), repl_list.end(),
	                  [&](const repl_node &n) { return n.replid == replid; });
	if (prepl_node == repl_list.end())
		return;
	auto &range_list = prepl_node->range_list;
	for (auto prange_node = range_list.begin();
	     prange_node != range_list.end(); ++prange_node) {
		if (value == prange_node->low_value &&
			value == prange_node->high_value) {
			range_list.erase(prange_node);
			return;
		} else if (value == prange_node->low_value) {
			prange_node->low_value ++;
			return;
		} else if (value == prange_node->high_value) {
			prange_node->high_value --;
			return;
		} else if (value > prange_node->low_value &&
			value < prange_node->high_value) {
			range_list.emplace(prange_node, prange_node->low_value, value - 1);
			prange_node->low_value = value + 1;
			return;
		}
	}
} catch (const std::bad_alloc &) {
	fprintf(stderr, "E-1615: ENOMEM\n");
}

BOOL idset::concatenate(const IDSET *pset_src)
{
	auto pset_dst = this;
	
	if (!pset_dst->b_serialize || !pset_src->b_serialize)
		return FALSE;
	auto &repl_list = pset_src->repl_list;
	for (auto prepl_node = repl_list.begin();
	     prepl_node != repl_list.end(); ++prepl_node) {
		for (const auto &range_node : prepl_node->range_list) {
			if (range_node.high_value == range_node.low_value) {
				if (!idset_append_internal(pset_dst,
				    prepl_node->replid, range_node.low_value))
					return FALSE;	
			} else {
				if (!append_range(prepl_node->replid,
				    range_node.low_value, range_node.high_value))
					return FALSE;	
			}
		}
	}
	return TRUE;
}

BOOL idset::hint(uint64_t eid)
{
	auto pset = this;
	
	if (!pset->b_serialize && pset->repl_type == REPL_TYPE_GUID)
		return FALSE;	
	auto replid = rop_util_get_replid(eid);
	auto value = rop_util_get_gc_value(eid);
	auto prepl_node = std::find_if(repl_list.begin(), repl_list.end(),
	                  [&](const repl_node &n) { return n.replid == replid; });
	if (prepl_node == repl_list.end())
		return FALSE;
	for (const auto &range_node : prepl_node->range_list) {
		if (value >= range_node.low_value && value <= range_node.high_value)
			return TRUE;
	}
	return FALSE;
}

static std::unique_ptr<BINARY, mdel> idset_init_binary()
{
	std::unique_ptr<BINARY, mdel> pbin(static_cast<BINARY *>(malloc(sizeof(BINARY))));
	if (NULL == pbin) {
		return NULL;
	}
	pbin->cb = 0;
	pbin->pv = malloc(4096);
	if (pbin->pv == nullptr) {
		return NULL;
	}
	return pbin;
}

static BOOL idset_write_to_binary(BINARY *pbin, const void *pb, uint8_t len)
{
	uint32_t alloc_len = strange_roundup(pbin->cb, 4096);
	if (pbin->cb + len >= alloc_len) {
		alloc_len = strange_roundup(pbin->cb + len, 4096);
		auto pdata = realloc(pbin->pb, alloc_len);
		if (NULL == pdata) {
			return FALSE;
		}
		pbin->pv = pdata;
	}
	memcpy(pbin->pb + pbin->cb, pb, len);
	pbin->cb += len;
	return TRUE;
}

static BOOL idset_encoding_push_command(BINARY *pbin,
    uint8_t length, const uint8_t *pcommon_bytes)
{
	if (length > 6) {
		return FALSE;
	}
	if (FALSE == idset_write_to_binary(pbin, &length, sizeof(uint8_t))) {
		return FALSE;
	}
	return idset_write_to_binary(pbin, pcommon_bytes, length);
}

static BOOL idset_encoding_pop_command(BINARY *pbin)
{
	uint8_t command = 0x50;
	return idset_write_to_binary(pbin, &command, sizeof(uint8_t));
}


static BOOL idset_encode_range_command(BINARY *pbin, uint8_t length,
    const uint8_t *plow_bytes, const uint8_t *phigh_bytes)
{
	if (length > 6 || 0 == length) {
		return FALSE;
	}
	uint8_t command = 0x52;
	if (FALSE == idset_write_to_binary(pbin, &command, sizeof(uint8_t))) {
		return FALSE;
	}
	if (FALSE == idset_write_to_binary(pbin, plow_bytes, length)) {
		return FALSE;
	}
	return idset_write_to_binary(pbin, phigh_bytes, length);
}

static BOOL idset_encode_end_command(BINARY *pbin)
{
	uint8_t command = 0;
	return idset_write_to_binary(pbin, &command, sizeof(uint8_t));
}

static uint8_t idset_stack_get_common_bytes(const byte_stack &stack, GLOBCNT &common_bytes)
{
	uint8_t common_length = 0;
	
	for (const auto &limb : stack) {
		if (common_length + limb.common_length <= 6)
			memcpy(&common_bytes.ab[common_length],
			       limb.pcommon_bytes, limb.common_length);
		common_length += limb.common_length;
	}
	return common_length;
}

static BOOL idset_encode_globset(BINARY *pbin, const std::vector<range_node> &globset)
{
	if (globset.size() == 1) {
		auto prange_node = globset.begin();
		auto common_bytes = rop_util_value_to_gc(prange_node->low_value);
		if (prange_node->high_value == prange_node->low_value) {
			if (!idset_encoding_push_command(pbin, 6, common_bytes.ab))
				return FALSE;
		} else {
			auto common_bytes1 = rop_util_value_to_gc(prange_node->high_value);
			if (!idset_encode_range_command(pbin, 6,
			    common_bytes.ab, common_bytes1.ab))
				return FALSE;
		}
		return idset_encode_end_command(pbin);
	}
	auto common_bytes = rop_util_value_to_gc(globset.front().low_value);
	auto common_bytes1 = rop_util_value_to_gc(globset.back().high_value);
	uint8_t stack_length;
	for (stack_length=0; stack_length<6; stack_length++) {
		if (common_bytes.ab[stack_length] != common_bytes1.ab[stack_length])
			break;
	}
	if (stack_length != 0 &&
	    !idset_encoding_push_command(pbin, stack_length, common_bytes.ab))
		return FALSE;
	for (const auto &range_node : globset) {
		common_bytes = rop_util_value_to_gc(range_node.low_value);
		if (range_node.high_value == range_node.low_value) {
			if (!idset_encoding_push_command(pbin,
			    6 - stack_length, &common_bytes.ab[stack_length]))
				return FALSE;
			continue;
		}
		common_bytes1 = rop_util_value_to_gc(range_node.high_value);
		int i;
		for (i=stack_length; i<6; i++) {
			if (common_bytes.ab[i] != common_bytes1.ab[i])
				break;
		}
		if (stack_length != i && !idset_encoding_push_command(pbin,
		    i - stack_length, &common_bytes.ab[stack_length]))
			return FALSE;
		if (!idset_encode_range_command(pbin, 6 - i,
		    &common_bytes.ab[i], &common_bytes1.ab[i]))
			return FALSE;
		if (stack_length != i && !idset_encoding_pop_command(pbin))
			return FALSE;
	}
	if (stack_length != 0 && !idset_encoding_pop_command(pbin))
		return FALSE;
	return idset_encode_end_command(pbin);
}

static BOOL idset_write_uint16(BINARY *pbin, uint16_t v)
{
	v = cpu_to_le16(v);
	return idset_write_to_binary(pbin, &v, sizeof(v));
}

static BOOL idset_write_uint32(BINARY *pbin, uint32_t v)
{
	v = cpu_to_le32(v);
	return idset_write_to_binary(pbin, &v, sizeof(v));
}

static BOOL idset_write_guid(BINARY *pbin, const GUID *pguid)
{
	if (FALSE == idset_write_uint32(pbin, pguid->time_low)) {
		return FALSE;
	}
	if (FALSE == idset_write_uint16(pbin, pguid->time_mid)) {
		return FALSE;
	}
	if (FALSE == idset_write_uint16(pbin, pguid->time_hi_and_version)) {
		return FALSE;
	}
	if (FALSE == idset_write_to_binary(pbin, pguid->clock_seq, 2)) {
		return FALSE;
	}
	if (FALSE == idset_write_to_binary(pbin, pguid->node, 6)) {
		return FALSE;
	}
	return TRUE;
}

BINARY *idset::serialize_replid() const
{
	auto pset = this;
	
	if (!pset->b_serialize)
		return NULL;
	auto pbin = idset_init_binary();
	if (NULL == pbin) {
		return NULL;
	}
	for (const auto &repl_node : repl_list) {
		if (repl_node.range_list.size() == 0)
			continue;
		if (!idset_write_uint16(pbin.get(), repl_node.replid) ||
		    !idset_encode_globset(pbin.get(), repl_node.range_list))
			return NULL;
	}
	return pbin.release();
}

BINARY *idset::serialize_replguid()
{
	auto pset = this;
	GUID tmp_guid;
	
	if (!pset->b_serialize)
		return NULL;
	if (NULL == pset->mapping) {
		return NULL;
	}
	auto pbin = idset_init_binary();
	if (NULL == pbin) {
		return NULL;
	}
	for (auto &repl_node : repl_list) {
		if (repl_node.range_list.size() == 0)
			continue;
		if (FALSE == pset->mapping(TRUE, pset->pparam,
		    &repl_node.replid, &tmp_guid))
			return NULL;
		if (!idset_write_guid(pbin.get(), &tmp_guid) ||
		    !idset_encode_globset(pbin.get(), repl_node.range_list))
			return NULL;
	}
	return pbin.release();
}

BINARY *idset::serialize()
{
	return repl_type == REPL_TYPE_ID ? serialize_replid() : serialize_replguid();
}

static uint32_t idset_decode_globset(const BINARY *pbin,
    std::vector<range_node> &globset) try
{
	uint32_t offset = 0;
	byte_stack bytes_stack;
	bytes_stack.reserve(6);
	
	while (offset < pbin->cb) {
		uint8_t command = pbin->pb[offset];
		offset ++;
		switch (command) {
		case 0x0: /* end */
			return offset;
		case 0x1:
		case 0x2:
		case 0x3:
		case 0x4:
		case 0x5:
		case 0x6: { /* push */
			GLOBCNT common_bytes;
			memcpy(common_bytes.ab, &pbin->pb[offset], command);
			offset += command;
			bytes_stack.emplace_back(common_bytes.ab, command);
			auto stack_length = idset_stack_get_common_bytes(bytes_stack, common_bytes);
			if (stack_length > 6) {
				debug_info("[idset]: length of common bytes in"
					" stack is too long when deserializing");
				return 0;
			}
			if (stack_length != 6)
				break;
			try {
				auto x = rop_util_gc_to_value(common_bytes);
				globset.emplace_back(x, x);
			} catch (const std::bad_alloc &) {
				fprintf(stderr, "E-1616: ENOMEM\n");
				return 0;
			}
			/* MS-OXCFXICS 3.1.5.4.3.1.1 */
			/* pop the stack without pop command */
			if (bytes_stack.size() > 0)
				bytes_stack.pop_back();
			break;
		}
		case 0x42: { /* bitmask */
			GLOBCNT common_bytes;
			uint8_t start_value = pbin->pb[offset++];
			uint8_t bitmask = pbin->pb[offset++];
			auto stack_length = idset_stack_get_common_bytes(bytes_stack, common_bytes);
			if (5 != stack_length) {
				debug_info("[idset]: bitmask command error when "
					"deserializing, length of common bytes in "
					"stack should be 5");
				return 0;
			}
			common_bytes.ab[5] = start_value;
			auto low_value = rop_util_gc_to_value(common_bytes);
			std::optional<range_node> prange_node;
			prange_node.emplace(low_value, low_value);
			for (int i = 0; i < 8; ++i) {
				if (!(bitmask & (1U << i))) {
					if (prange_node.has_value()) {
						globset.push_back(std::move(*prange_node));
						prange_node.reset();
					}
				} else if (!prange_node.has_value()) {
					prange_node.emplace(low_value + i + 1, prange_node->low_value);
				} else {
					prange_node->high_value ++;
				}
			}
			if (prange_node.has_value()) {
				globset.push_back(std::move(*prange_node));
				//prange_node.reset();
			}
			break;
		}
		case 0x50: /* pop */
			if (bytes_stack.size() > 0)
				bytes_stack.pop_back();
			break;
		case 0x52: { /* range */
			GLOBCNT common_bytes;
			auto stack_length = idset_stack_get_common_bytes(bytes_stack, common_bytes);
			if (stack_length > 5) {
				debug_info("[idset]: range command error when "
					"deserializing, length of common bytes in "
					"stack should be less than 5");
				return 0;
			}
			memcpy(&common_bytes.ab[stack_length],
				pbin->pb + offset, 6 - stack_length);
			offset += 6 - stack_length;
			auto low_value = rop_util_gc_to_value(common_bytes);
			memcpy(&common_bytes.ab[stack_length],
				pbin->pb + offset, 6 - stack_length);
			offset += 6 - stack_length;
			auto high_value = rop_util_gc_to_value(common_bytes);
			globset.emplace_back(low_value, high_value);
			break;
		}
		}
	}
	return 0;
} catch (const std::bad_alloc &) {
	fprintf(stderr, "E-1618: ENOMEM\n");
	return 0;
}

static void idset_read_guid(const void *pv, uint32_t offset, GUID *pguid)
{
	auto pb = static_cast<const uint8_t *>(pv);
	pguid->time_low = le32p_to_cpu(&pb[offset]);
	offset += sizeof(uint32_t);
	pguid->time_mid = le16p_to_cpu(&pb[offset]);
	offset += sizeof(uint16_t);
	pguid->time_hi_and_version = le16p_to_cpu(&pb[offset]);
	offset += sizeof(uint16_t);
	memcpy(pguid->clock_seq, pb + offset, 2);
	offset += 2;
	memcpy(pguid->node, pb + offset, 6);
}

BOOL idset::deserialize(const BINARY *pbin) try
{
	auto pset = this;
	uint32_t offset = 0;
	
	if (pset->b_serialize)
		return FALSE;
	while (offset < pbin->cb) {
		repl_node repl_node;

		if (REPL_TYPE_ID == pset->repl_type) {
			repl_node.replid = le16p_to_cpu(&pbin->pb[offset]);
			offset += sizeof(uint16_t);
		} else {
			idset_read_guid(pbin->pb, offset, &repl_node.replguid);
			offset += 16;
		}
		if (offset >= pbin->cb) {
			return FALSE;
		}
		BINARY bin1;
		bin1.pb = pbin->pb + offset;
		bin1.cb = pbin->cb - offset;
		uint32_t length = idset_decode_globset(&bin1, repl_node.range_list);
		if (0 == length) {
			return FALSE;
		}
		offset += length;
		repl_list.push_back(std::move(repl_node));
	}
	return TRUE;
} catch (const std::bad_alloc &) {
	fprintf(stderr, "E-1617: ENOMEM\n");
	return false;
}

BOOL idset::convert() try
{
	auto pset = this;
	std::vector<repl_node> temp_list;
	
	if (pset->b_serialize)
		return FALSE;
	if (REPL_TYPE_GUID == pset->repl_type) {
		if (NULL == pset->mapping) {
			return FALSE;
		}
		for (auto &replguid_node : repl_list) {
			uint16_t replid;
			if (FALSE == pset->mapping(FALSE, pset->pparam,
			    &replid, &replguid_node.replguid))
				return false;
			repl_node repl_node;
			repl_node.replid = replid;
			repl_node.range_list = replguid_node.range_list;
			temp_list.push_back(std::move(repl_node));
		}
		repl_list = std::move(temp_list);
	}
	pset->b_serialize = true;
	return TRUE;
} catch (const std::bad_alloc &) {
	fprintf(stderr, "E-1619: ENOMEM\n");
	return false;
}

BOOL idset::get_repl_first_max(uint16_t replid, uint64_t *peid)
{
	auto pset = this;
	const std::vector<range_node> *prange_list = nullptr;
	
	if (!pset->b_serialize && pset->repl_type == REPL_TYPE_GUID) {
		if (NULL == pset->mapping) {
			return FALSE;
		}
		for (auto &replguid_node : repl_list) {
			uint16_t tmp_replid;
			if (FALSE == pset->mapping(FALSE, pset->pparam,
			    &tmp_replid, &replguid_node.replguid))
				return FALSE;
			if (tmp_replid == replid) {
				prange_list = &replguid_node.range_list;
				break;
			}
		}
	} else {
		for (const auto &repl_node : repl_list) {
			if (replid == repl_node.replid) {
				prange_list = &repl_node.range_list;
				break;
			}
		}
	}
	if (NULL == prange_list) {
		*peid = rop_util_make_eid_ex(replid, 0);
		return TRUE;
	}
	auto pnode = prange_list->begin();
	if (pnode == prange_list->end())
		*peid = rop_util_make_eid_ex(replid, 0);
	else
		*peid = rop_util_make_eid_ex(replid, pnode->high_value);
	return TRUE;
}

BOOL idset::enum_replist(void *pparam, REPLIST_ENUM replist_enum)
{
	auto pset = this;
	
	if (pset->b_serialize || pset->repl_type != REPL_TYPE_GUID) {
		for (const auto &repl_node : repl_list) {
			replist_enum(pparam, repl_node.replid);
		}
		return TRUE;
	}
	if (NULL == pset->mapping) {
		return FALSE;
	}
	for (auto &replguid_node : repl_list) {
		uint16_t tmp_replid;
		if (FALSE == pset->mapping(FALSE, pset->pparam,
		    &tmp_replid, &replguid_node.replguid))
			return FALSE;
		replist_enum(pparam, tmp_replid);
	}
	return TRUE;
}

BOOL idset::enum_repl(uint16_t replid, void *pparam, REPLICA_ENUM repl_enum)
{
	auto pset = this;
	std::vector<range_node> *prange_list = nullptr;
	
	if (!pset->b_serialize && pset->repl_type == REPL_TYPE_GUID) {
		if (NULL == pset->mapping) {
			return FALSE;
		}
		for (auto &replguid_node : repl_list) {
			uint16_t tmp_replid;
			if (FALSE == pset->mapping(FALSE, pset->pparam,
			    &tmp_replid, &replguid_node.replguid))
				return FALSE;
			if (tmp_replid == replid) {
				prange_list = &replguid_node.range_list;
				break;
			}
		}
	} else {
		for (auto &repl_node : repl_list) {
			if (replid == repl_node.replid) {
				prange_list = &repl_node.range_list;
				break;
			}
		}
	}
	if (NULL == prange_list) {
		return TRUE;
	}
	for (auto &range_node : *prange_list) {
		for (auto ival = range_node.low_value;
		     ival <= range_node.high_value; ++ival) {
			auto tmp_eid = rop_util_make_eid_ex(replid, ival);
			repl_enum(pparam, tmp_eid);
		}
	}
	return TRUE;
}
