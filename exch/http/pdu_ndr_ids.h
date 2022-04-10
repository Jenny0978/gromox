#pragma once

enum {
	DCERPC_SECURITY_CONTEXT_MULTIPLEXING = 1,
	DCERPC_CONNECTION_ORPHAN_SUPPORTED = 2,
};

enum {
	DCERPC_AUTH_TYPE_NONE = 0,
	DCERPC_AUTH_TYPE_KRB5_1 = 1,
	DCERPC_AUTH_TYPE_SPNEGO = 9,
	DCERPC_AUTH_TYPE_NTLMSSP = 10,
	DCERPC_AUTH_TYPE_KRB5 = 16,
	DCERPC_AUTH_TYPE_DPA = 17,
	DCERPC_AUTH_TYPE_MSN = 18,
	DCERPC_AUTH_TYPE_DIGEST = 21,
	DCERPC_AUTH_TYPE_SCHANNEL = 68,
	DCERPC_AUTH_TYPE_MSMQ = 100,
	DCERPC_AUTH_TYPE_NCALRPC_AS_SYSTEM = 200,
};

enum {
	RPC_C_AUTHN_LEVEL_DEFAULT = 0, /* rpc_c_protect_level_default */
	RPC_C_AUTHN_LEVEL_NONE = 1, /* rpc_c_protect_level_none */
	RPC_C_AUTHN_LEVEL_CONNECT = 2, /* rpc_c_protect_level_connect */
	RPC_C_AUTHN_LEVEL_CALL = 3, /* rpc_c_protect_level_call */
	RPC_C_AUTHN_LEVEL_PKT = 4, /* rpc_c_protect_level_pkt */
	RPC_C_AUTHN_LEVEL_PKT_INTEGRITY = 5, /* rpc_c_protect_level_pkt_integrity */
	RPC_C_AUTHN_LEVEL_PKT_PRIVACY = 6, /* rpc_c_protect_level_pkt_privacy */
};

enum {
	DCERPC_PKT_REQUEST = 0,
	DCERPC_PKT_PING = 1,
	DCERPC_PKT_RESPONSE = 2,
	DCERPC_PKT_FAULT = 3,
	DCERPC_PKT_WORKING = 4,
	DCERPC_PKT_NOCALL = 5,
	DCERPC_PKT_REJECT = 6,
	DCERPC_PKT_ACK = 7,
	DCERPC_PKT_FACK = 9,
	DCERPC_PKT_CANCEL_ACK = 10,
	DCERPC_PKT_BIND = 11,
	DCERPC_PKT_BIND_ACK = 12,
	DCERPC_PKT_BIND_NAK = 13,
	DCERPC_PKT_ALTER = 14,
	DCERPC_PKT_ALTER_ACK = 15,
	DCERPC_PKT_AUTH3 = 16,
	DCERPC_PKT_SHUTDOWN = 17,
	DCERPC_PKT_CO_CANCEL = 18,
	DCERPC_PKT_ORPHANED = 19,
	DCERPC_PKT_RTS = 20,
};

enum {
	DCERPC_BIND_RESULT_USER_REJECTION = 1,
	DCERPC_BIND_RESULT_PROVIDER_REJECT = 2,
	DCERPC_BIND_RESULT_NEGOTIATE_ACK = 3,
};

enum {
	DCERPC_BIND_REASON_NOT_SPECIFIED = 0,
	DCERPC_BIND_REASON_ASYNTAX = 1,
	DECRPC_BIND_REASON_LOCAL_LIMIT_EXCEEDED = 2,
	DECRPC_BIND_REASON_VERSION_NOT_SUPPORTED = 4,
	DCERPC_BIND_REASON_INVALID_AUTH_TYPE = 8,
	DCERPC_BIND_REASON_INVALID_CHECKSUM = 9,
};

enum {
	DCERPC_FAULT_SUCCESS = 0x0,
	DCERPC_FAULT_COMM_FAILURE = 0x1C010001,
	DCERPC_FAULT_OP_RNG_ERROR = 0x1c010002,
	DCERPC_FAULT_UNK_IF = 0x1c010003,
	DCERPC_FAULT_NDR = 0x000006f7,
	DCERPC_FAULT_INVALID_TAG = 0x1c000006,
	DCERPC_FAULT_CONTEXT_MISMATCH = 0x1c00001a,
	DCERPC_FAULT_OTHER = 0x00000001,
	DCERPC_FAULT_ACCESS_DENIED = 0x00000005,
	DCERPC_FAULT_CANT_PERFORM = 0x000006d8,
	DCERPC_FAULT_SEC_PKG_ERROR = 0x00000721,
	DCERPC_FAULT_TODO = 0x00000042,
};

enum {
	DCERPC_PFC_FLAG_FIRST = 0x01,
	DCERPC_PFC_FLAG_LAST = 0x02,
	DCERPC_PFC_FLAG_PENDING_CANCEL = 0x04,
	DCERPC_PFC_FLAG_SUPPORT_HEADER_SIGN = 0x04,
	DCERPC_PFC_FLAG_CONC_MPX = 0x10,
	DCERPC_PFC_FLAG_DID_NOT_EXECUTE = 0x20,
	DCERPC_PFC_FLAG_MAYBE = 0x40,
	DCERPC_PFC_FLAG_OBJECT_UUID = 0x80,
};

enum {
	DCERPC_REQUEST_LENGTH = 24,
	DCERPC_RESPONSE_LENGTH = 24,
	DCERPC_AUTH_LEVEL_DEFAULT = 2,
	DCERPC_AUTH_TRAILER_LENGTH = 8,
	DCERPC_PTYPE_OFFSET = 2,
	DCERPC_PFC_OFFSET = 3,
	DCERPC_DREP_OFFSET = 4,
	DCERPC_FRAG_LEN_OFFSET = 8,
	DCERPC_AUTH_LEN_OFFSET = 10,
	DCERPC_DREP_LE = 0x10,
};

enum {
	RTS_IPV4 = 0,
	RTS_IPV6 = 1,
};

enum {
	FD_CLIENT = 0,
	FD_INROXY = 1,
	FD_SERVER = 2,
	FD_OUTPROXY = 3,
};

enum {
	RTS_CMD_RECEIVE_WINDOW_SIZE  = 0,
	RTS_CMD_FLOW_CONTROL_ACK = 1,
	RTS_CMD_CONNECTION_TIMEOUT = 2,
	RTS_CMD_COOKIE = 3,
	RTS_CMD_CHANNEL_LIFETIME = 4,
	RTS_CMD_CLIENT_KEEPALIVE = 5,
	RTS_CMD_VERSION = 6,
	RTS_CMD_EMPTY = 7,
	RTS_CMD_PADDING = 8,
	RTS_CMD_NEGATIVE_ANCE = 9,
	RTS_CMD_ANCE = 10,
	RTS_CMD_CLIENT_ADDRESS = 11,
	RTS_CMD_ASSOCIATION_GROUP_ID = 12,
	RTS_CMD_DESTINATION = 13,
	RTS_CMD_PING_TRAFFIC_SENT_NOTIFY = 14,
};

enum {
	RTS_FLAG_NONE = 0,
	RTS_FLAG_PING = 1 << 0,
	RTS_FLAG_OTHER_CMD = 1 << 1,
	RTS_FLAG_RECYCLE_CHANNEL = 1 << 2,
	RTS_FLAG_IN_CHANNEL = 1 << 3,
	RTS_FLAG_OUT_CHANNEL = 1 << 4,
	RTS_FLAG_EOF = 1 << 5,
	RTS_FLAG_ECHO = 1 << 6,
};

#ifdef __cplusplus
extern "C" {
#endif

extern const char *dcepkt_idtoname(unsigned int);

#ifdef __cplusplus
}
#endif
