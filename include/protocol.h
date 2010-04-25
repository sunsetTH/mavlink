#ifndef  _MAVLINK_PROTOCOL_H_
#define  _MAVLINK_PROTOCOL_H_

#include "string.h"
#include "checksum.h"

#include "mavlink_types.h"

/**
 * @brief Finalize a MAVLink message
 *
 * This function calculates the checksum and sets length and aircraft id correctly.
 * It assumes that the message id and the payload are already correctly set.
 *
 * @param msg Message to finalize
 * @param system_id Id of the sending (this) system, 1-127
 * @param length Message length, usually just the counter incremented while packing the message
 */
static inline uint16_t finalize_message(mavlink_message_t* msg, uint8_t system_id, uint8_t component_id, uint16_t length)
{
	// This code part is the same for all messages;

	msg->len = length;
	msg->sysid = system_id;
	msg->compid = component_id;
	// One sequence number per component
	static uint8_t seq = 0;
	msg->seq = seq++;

	uint16_t checksum = crc_calculate((uint8_t*)((void*)msg), length + MAVLINK_CORE_HEADER_LEN);
	msg->ck_a = (uint8_t)(checksum & 0xFF); ///< High byte
	msg->ck_b = (uint8_t)(checksum >> 8); ///< Low byte

	return length + MAVLINK_NUM_NON_STX_PAYLOAD_BYTES;
}

/**
 * @brief Pack a message to send it over a serial byte stream
 */
static inline uint16_t message_to_send_buffer(uint8_t* buffer, const mavlink_message_t* msg)
{
	*(buffer+0) = MAVLINK_STX; ///< Start transmit
	memcpy((buffer+1), msg, msg->len + MAVLINK_CORE_HEADER_LEN); ///< Core header plus payload
        *(buffer + msg->len + MAVLINK_CORE_HEADER_LEN + 1) = msg->ck_a;
        *(buffer + msg->len + MAVLINK_CORE_HEADER_LEN + 2) = msg->ck_b;
        return msg->len + MAVLINK_NUM_NON_PAYLOAD_BYTES;
        return 0;
}

/**
 * @brief Get the required buffer size for this message
 */
static inline uint16_t message_get_send_buffer_length(const mavlink_message_t* msg)
{
        return msg->len + MAVLINK_NUM_NON_PAYLOAD_BYTES;
}

union checksum_ {
	uint16_t s;
	uint8_t c[2];
};


static inline void mavlink_start_checksum(mavlink_message_t* msg)
{
	union checksum_ ck;
	crcInit(&(ck.s));
	msg->ck_a = ck.c[0];
	msg->ck_b = ck.c[1];
}

static inline void mavlink_update_checksum(mavlink_message_t* msg, uint8_t c)
{
	union checksum_ ck;
	ck.c[0] = msg->ck_a;
	ck.c[1] = msg->ck_b;
	crcAccumulate(c, &(ck.s));
	msg->ck_a = ck.c[0];
	msg->ck_b = ck.c[1];
}


/**
 * @brief Initialize the communication stack
 *
 * This function has to be called before using commParseBuffer() to initialize the different status registers.
 *
 * @return Will initialize the different buffers and status registers.
 */
static void mavlink_parse_state_initialize(mavlink_status_t* initStatus)
{
        if ((initStatus->parse_state <= MAVLINK_PARSE_STATE_UNINIT) || (initStatus->parse_state > MAVLINK_PARSE_STATE_GOT_CRC1))
	{
                initStatus->ck_a = 0;
                initStatus->ck_b = 0;
                initStatus->msg_received = 0;
                initStatus->buffer_overrun = 0;
                initStatus->parse_error = 0;
                initStatus->parse_state = MAVLINK_PARSE_STATE_UNINIT;
                initStatus->packet_idx = 0;
                initStatus->packet_rx_drop_count = 0;
                initStatus->packet_rx_success_count = 0;
	}
}

	/**
	 * This is a convenience function which handles the complete MAVLink parsing.
	 * the function will parse one byte at a time and return the complete packet once
	 * it could be successfully decoded. Checksum and other failures will be silently
	 * ignored.
	 *
	 * @param chan     ID of the current channel. This allows to parse different channels with this function.
	 *                 a channel is not a physical message channel like a serial port, but a logic partition of
	 *                 the communication streams in this case. COMM_NB is the limit for the number of channels
	 *                 on MCU (e.g. ARM7), while COMM_NB_HIGH is the limit for the number of channels in Linux/Windows
	 * @param c        The char to barse
	 *
	 * @param returnMsg NULL if no message could be decoded, the message data else
	 * @return 0 if no message could be decoded, 1 else
	 *
	 * A typical use scenario of this function call is:
	 *
	 * @code
	 * #include <inttypes.h> // For fixed-width uint8_t type
	 *
	 * mavlink_message_t msg;
	 * int chan = 0;
	 *
	 *
	 * while(serial.bytesAvailable > 0)
	 * {
	 *   uint8_t byte = serial.getNextByte();
	 *   if (mavlink_parse_char(chan, byte, &msg))
	 *     {
	 *     printf("Received message with ID %d, sequence: %d from component %d of system %d", msg.msgid, msg.seq, msg.compid, msg.sysid);
	 *     }
	 * }
	 *
	 *
	 * @endcode
	 */
static inline uint8_t mavlink_parse_char(uint8_t chan, uint8_t c, mavlink_message_t* r_message, mavlink_status_t* r_mavlink_status)
{
#if (defined linux) | (defined __linux) | (defined  __MACH__) | (defined _WIN32)
        static mavlink_status_t m_mavlink_status[MAVLINK_COMM_NB_HIGH];
        static mavlink_message_t m_mavlink_message[MAVLINK_COMM_NB_HIGH];
#else
	static mavlink_status_t m_mavlink_status[MAVLINK_COMM_NB];
	static mavlink_message_t m_mavlink_message[MAVLINK_COMM_NB];
#endif
	// Initializes only once, values keep unchanged after first initialization
        mavlink_parse_state_initialize(&m_mavlink_status[chan]);

	mavlink_message_t* rxmsg = &m_mavlink_message[chan]; ///< The currently decoded message
	mavlink_status_t* status = &m_mavlink_status[chan]; ///< The current decode status
	int bufferIndex = 0;

	status->msg_received = 0;

	switch (status->parse_state)
	{
	case MAVLINK_PARSE_STATE_UNINIT:
	case MAVLINK_PARSE_STATE_IDLE:
                if (c == MAVLINK_STX)
		{
			status->parse_state = MAVLINK_PARSE_STATE_GOT_STX;
			mavlink_start_checksum(rxmsg);
		}
		break;

	case MAVLINK_PARSE_STATE_GOT_STX:
		if (status->msg_received)
		{
			status->buffer_overrun++;
			status->parse_error++;
			status->msg_received = 0;
			status->parse_state = MAVLINK_PARSE_STATE_IDLE;
		}
		else
		{
			// NOT counting STX, LENGTH, SEQ, SYSID, COMPID, MSGID, CRC1 and CRC2
			rxmsg->len = c;
			status->packet_idx = 0;
			mavlink_update_checksum(rxmsg, c);
			status->parse_state = MAVLINK_PARSE_STATE_GOT_LENGTH;
		}
		break;

	case MAVLINK_PARSE_STATE_GOT_LENGTH:
		rxmsg->seq = c;
		mavlink_update_checksum(rxmsg, c);
		status->parse_state = MAVLINK_PARSE_STATE_GOT_SEQ;
		break;

	case MAVLINK_PARSE_STATE_GOT_SEQ:
		rxmsg->sysid = c;
		mavlink_update_checksum(rxmsg, c);
		status->parse_state = MAVLINK_PARSE_STATE_GOT_SYSID;
		break;

	case MAVLINK_PARSE_STATE_GOT_SYSID:
		rxmsg->compid = c;
		mavlink_update_checksum(rxmsg, c);
		status->parse_state = MAVLINK_PARSE_STATE_GOT_COMPID;
		break;

	case MAVLINK_PARSE_STATE_GOT_COMPID:
		rxmsg->msgid = c;
		mavlink_update_checksum(rxmsg, c);
		if (rxmsg->len == 0)
		{
			status->parse_state = MAVLINK_PARSE_STATE_GOT_PAYLOAD;
		}
		else
		{
			status->parse_state = MAVLINK_PARSE_STATE_GOT_MSGID;
		}
		break;

	case MAVLINK_PARSE_STATE_GOT_MSGID:
		rxmsg->payload[status->packet_idx++] = c;
		mavlink_update_checksum(rxmsg, c);
		if (status->packet_idx == rxmsg->len)
		{
			status->parse_state = MAVLINK_PARSE_STATE_GOT_PAYLOAD;
		}
		break;

	case MAVLINK_PARSE_STATE_GOT_PAYLOAD:
		if (c != rxmsg->ck_a)
		{
			// Check first checksum byte
			status->parse_error++;
			status->msg_received = 0;
			status->parse_state = MAVLINK_PARSE_STATE_IDLE;
		}
		else
		{
			status->parse_state = MAVLINK_PARSE_STATE_GOT_CRC1;
		}
		break;

	case MAVLINK_PARSE_STATE_GOT_CRC1:
		if (c != rxmsg->ck_b)
		{// Check second checksum byte
			status->parse_error++;
			status->msg_received = 0;
			status->parse_state = MAVLINK_PARSE_STATE_IDLE;
		}
		else
		{
			// Successfully got message
			status->msg_received = 1;
			status->parse_state = MAVLINK_PARSE_STATE_IDLE;
			memcpy(r_message, rxmsg, sizeof(mavlink_message_t));
		}
		break;
	}

	bufferIndex++;
	// If a message has been sucessfully decoded, check index
	if (status->msg_received == 1)
	{
                while(status->current_seq != rxmsg->seq)
		{
			status->packet_rx_drop_count++;
                        status->current_seq++;
		}
		status->current_seq = rxmsg->seq;
		// Initial condition: If no packet has been received so far, drop count is undefined
		if (status->packet_rx_success_count == 0) status->packet_rx_drop_count = 0;
		// Count this packet as received
		status->packet_rx_success_count++;
	}

        r_mavlink_status->current_seq = status->current_seq+1;
	r_mavlink_status->packet_rx_success_count = status->packet_rx_success_count;
	r_mavlink_status->packet_rx_drop_count = status->packet_rx_drop_count;
	return status->msg_received;
}

typedef union __generic_16bit
{
	uint8_t b[2];
	int16_t s;
} generic_16bit;

typedef union __generic_32bit
	{
		uint8_t b[4];
		float f;
		int32_t i;
		int16_t s;
	} generic_32bit;

typedef union __generic_64bit
	{
		uint8_t b[8];
		int64_t ll; ///< Long long (64 bit)
	} generic_64bit;

/**
 * @brief Place an unsigned byte into the buffer
 *
 * @param b the byte to add
 * @param bindex the position in the packet
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline uint8_t put_uint8_t_by_index(uint8_t b, uint8_t bindex, uint8_t* buffer)
{
	*(buffer + bindex) = b;
	return sizeof(b);
}

/**
 * @brief Place a signed byte into the buffer
 *
 * @param b the byte to add
 * @param bindex the position in the packet
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline uint8_t put_int8_by_index(int8_t b, int8_t bindex, uint8_t* buffer)
{
	*(buffer + bindex) = (uint8_t)b;
	return sizeof(b);
}

/**
 * @brief Place two unsigned bytes into the buffer
 *
 * @param b the bytes to add
 * @param bindex the position in the packet
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline uint8_t put_uint16_t_by_index(uint16_t b, const uint8_t bindex, uint8_t* buffer)
{
	buffer[bindex]   = (b>>8)&0xff;
	buffer[bindex+1] = (b & 0xff);
	return sizeof(b);
}

/**
 * @brief Place two signed bytes into the buffer
 *
 * @param b the bytes to add
 * @param bindex the position in the packet
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline uint8_t put_int16_t_by_index(int16_t b, uint8_t bindex, uint8_t* buffer)
{
	return put_uint16_t_by_index(b, bindex, buffer);
}

/**
 * @brief Place four unsigned bytes into the buffer
 *
 * @param b the bytes to add
 * @param bindex the position in the packet
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline uint8_t put_uint32_t_by_index(uint32_t b, const uint8_t bindex, uint8_t* buffer)
{
	buffer[bindex]   = (b>>24)&0xff;
	buffer[bindex+1] = (b>>16)&0xff;
	buffer[bindex+2] = (b>>8)&0xff;
	buffer[bindex+3] = (b & 0xff);
	return sizeof(b);
}

/**
 * @brief Place four signed bytes into the buffer
 *
 * @param b the bytes to add
 * @param bindex the position in the packet
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline uint8_t put_int32_t_by_index(int32_t b, uint8_t bindex, uint8_t* buffer)
{
	buffer[bindex]   = (b>>24)&0xff;
	buffer[bindex+1] = (b>>16)&0xff;
	buffer[bindex+2] = (b>>8)&0xff;
	buffer[bindex+3] = (b & 0xff);
	return sizeof(b);
}

/**
 * @brief Place four unsigned bytes into the buffer
 *
 * @param b the bytes to add
 * @param bindex the position in the packet
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline uint8_t put_uint64_t_by_index(uint64_t b, const uint8_t bindex, uint8_t* buffer)
{
	buffer[bindex]   = (b>>56)&0xff;
	buffer[bindex+1] = (b>>48)&0xff;
	buffer[bindex+2] = (b>>40)&0xff;
	buffer[bindex+3] = (b>>32)&0xff;
	buffer[bindex+4] = (b>>24)&0xff;
	buffer[bindex+5] = (b>>16)&0xff;
	buffer[bindex+6] = (b>>8)&0xff;
	buffer[bindex+7] = (b & 0xff);
	return sizeof(b);
}

/**
 * @brief Place four signed bytes into the buffer
 *
 * @param b the bytes to add
 * @param bindex the position in the packet
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline uint8_t put_int64_t_by_index(int64_t b, uint8_t bindex, uint8_t* buffer)
{
	return put_uint64_t_by_index(b, bindex, buffer);
}

/**
 * @brief Place a float into the buffer
 *
 * @param b the float to add
 * @param bindex the position in the packet
 * @param buffer the packet buffer
 * @return the new position of the last used byte in the buffer
 */
static inline uint8_t put_float_by_index(float b, uint8_t bindex, uint8_t* buffer)
{
	generic_32bit g;
	g.f = b;
	return put_int32_t_by_index(g.i, bindex, buffer);
}

/**
 * @brief Place an array into the buffer
 *
 * @param b the array to add
 * @param length size of the array (for strings: length WITHOUT '\0' char)
 * @param bindex the position in the packet
 * @param buffer packet buffer
 * @return new position of the last used byte in the buffer
 */
static inline uint8_t put_array_by_index(const int8_t* b, uint8_t length, uint8_t bindex, uint8_t* buffer)
{
	memcpy(buffer+bindex, b, length);
	return length;
}

#if !(defined linux) && !(defined __linux) && !(defined  __MACH__) && !(defined _WIN32)

// To make MAVLink work on your MCU, define a similar function

/*
void comm_send_ch(mavlink_channel_t chan, uint8_t ch)
{
    if (chan == MAVLINK_COMM_0)
    {
        uart0_transmit(ch);
    }
    if (chan == MAVLINK_COMM_1)
    {
    	uart1_transmit(ch);
    }
}
 */


static inline void mavlink_send_uart(mavlink_channel_t chan, mavlink_message_t* msg)
{
	// ARM7 MCU board implementation
	// Create pointer on message struct
	// Send STX
	comm_send_ch(chan, MAVLINK_STX);
	comm_send_ch(chan, msg->len);
	comm_send_ch(chan, msg->seq);
	comm_send_ch(chan, msg->sysid);
	comm_send_ch(chan, msg->compid);
	comm_send_ch(chan, msg->msgid);
	for(uint16_t i = 0; i < msg->len; i++)
	{
		comm_send_ch(chan, msg->payload[i]);
	}
	comm_send_ch(chan, msg->ck_a);
	comm_send_ch(chan, msg->ck_b);
}

static inline void send_debug_string(mavlink_channel_t chan, uint8_t* string)
{
	while(*string){
		comm_send_ch(chan, *string++);
	}
}
#endif

#endif /* _MAVLINK_PROTOCOL_H_ */