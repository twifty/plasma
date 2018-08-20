#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>

#include <linux/mutex.h>

#include "plasma.h"

MODULE_LICENSE("GPL v2");

LOGO_ATTRIBUTE(enabled)
LOGO_ATTRIBUTE(effect)
LOGO_ATTRIBUTE(color)
LOGO_ATTRIBUTE(random)
LOGO_ATTRIBUTE(brightness)
LOGO_ATTRIBUTE(speed)

FAN_ATTRIBUTE(enabled)
FAN_ATTRIBUTE(effect)
FAN_ATTRIBUTE(color)
FAN_ATTRIBUTE(random)
FAN_ATTRIBUTE(brightness)
FAN_ATTRIBUTE(speed)
FAN_ATTRIBUTE(mirage)
FAN_ATTRIBUTE(mirage_slot1)
FAN_ATTRIBUTE(mirage_slot2)
FAN_ATTRIBUTE(mirage_slot3)

RING_ATTRIBUTE(enabled)
RING_ATTRIBUTE(effect)
RING_ATTRIBUTE(color)
RING_ATTRIBUTE(random)
RING_ATTRIBUTE(brightness)
RING_ATTRIBUTE(speed)
RING_ATTRIBUTE(direction)
RING_ATTRIBUTE(repeat)
RING_ATTRIBUTE(morse)
RING_ATTRIBUTE(morse_slot1)
RING_ATTRIBUTE(morse_slot2)
RING_ATTRIBUTE(morse_slot3)

static struct attribute *logo_attrs[] = {
	LOGO_ATTRIBUTE_ADDR(enabled),
	LOGO_ATTRIBUTE_ADDR(effect),
	LOGO_ATTRIBUTE_ADDR(color),
	LOGO_ATTRIBUTE_ADDR(random),
	LOGO_ATTRIBUTE_ADDR(brightness),
	LOGO_ATTRIBUTE_ADDR(speed),
	NULL,
};

static struct attribute *fan_attrs[] = {
	FAN_ATTRIBUTE_ADDR(enabled),
	FAN_ATTRIBUTE_ADDR(effect),
	FAN_ATTRIBUTE_ADDR(color),
	FAN_ATTRIBUTE_ADDR(random),
	FAN_ATTRIBUTE_ADDR(brightness),
	FAN_ATTRIBUTE_ADDR(speed),
	FAN_ATTRIBUTE_ADDR(mirage),
	FAN_ATTRIBUTE_ADDR(mirage_slot1),
	FAN_ATTRIBUTE_ADDR(mirage_slot2),
	FAN_ATTRIBUTE_ADDR(mirage_slot3),
	NULL,
};

static struct attribute *ring_attrs[] = {
	RING_ATTRIBUTE_ADDR(enabled),
	RING_ATTRIBUTE_ADDR(effect),
	RING_ATTRIBUTE_ADDR(color),
	RING_ATTRIBUTE_ADDR(random),
	RING_ATTRIBUTE_ADDR(brightness),
	RING_ATTRIBUTE_ADDR(speed),
	RING_ATTRIBUTE_ADDR(direction),
	RING_ATTRIBUTE_ADDR(repeat),
	RING_ATTRIBUTE_ADDR(morse),
	RING_ATTRIBUTE_ADDR(morse_slot1),
	RING_ATTRIBUTE_ADDR(morse_slot2),
	RING_ATTRIBUTE_ADDR(morse_slot3),
	NULL,
};

static struct attribute_group logo_attr_group = {
	.name  = "logo",
	.attrs = logo_attrs,
};

static struct attribute_group fan_attr_group = {
	.name  = "fan",
	.attrs = fan_attrs,
};

static struct attribute_group ring_attr_group = {
	.name  = "ring",
	.attrs = ring_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&logo_attr_group,
	&fan_attr_group,
	&ring_attr_group,
	NULL
};






/* Structure to hold all of our device specific stuff */
struct usb_plasma {
	struct usb_device		*udev;						/* the usb device for this device */
	struct usb_interface	*interface;					/* the interface for this device */
	struct semaphore		limit_sem;					/* limiting the number of writes in progress */
	struct usb_anchor		submitted;					/* in case we need to retract our submissions */
	struct urb				*packet_in_urb;				/* the urb to read data with */
	byte					*packet_buffer;				/* the buffer to receive data */
	byte					*packet_cache;
	byte					*packet_selection;
	size_t					packet_size;				/* the size of the receive buffer */
	// size_t					packet_in_filled;			/* number of bytes in the buffer */
	// size_t					packet_in_copied;			/* already copied to user space */
	__u8					packet_in_endpointAddr;		/* the address of the interrupt in endpoint */
	__u8					packet_out_endpointAddr;	/* the address of the interrupt out endpoint */
	int						error;						/* the last request tanked */
	// bool					ongoing_read;				/* a read is going on */
	spinlock_t				err_lock;					/* lock for errors */
	struct kref				kref;
	struct mutex			io_mutex;					/* synchronize I/O with disconnect */
	wait_queue_head_t		packet_in_wait;				/* to wait for an ongoing read */
};
#define to_plasma_dev(d) container_of(d, struct usb_plasma, kref)

static struct usb_driver plasma_driver;
static void plasma_draw_down(struct usb_plasma *dev);

static void plasma_delete(struct kref *kref)
{
	struct usb_plasma *plasma = to_plasma_dev(kref);

	usb_free_urb(plasma->packet_in_urb);
	usb_put_dev(plasma->udev);
	kfree(plasma->packet_buffer);
	kfree(plasma->packet_cache);
	kfree(plasma);
}

static const struct file_operations plasma_fops = {
	.owner =	THIS_MODULE,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver plasma_class = {
	.name =			"plasma%d",
	.fops =			&plasma_fops,
	.minor_base =	USB_PLASMA_MINOR_BASE,
};

static int initialize_cache(struct usb_plasma *plasma);

static int plasma_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	int error;
	struct usb_plasma *plasma;
	struct usb_endpoint_descriptor *packet_in, *packet_out;
	// struct device *intf_dev;

	/* allocate memory for our device state and initialize it */
	if (NULL == (plasma = kzalloc(sizeof(*plasma), GFP_KERNEL)))
		return -ENOMEM;

	kref_init(&plasma->kref);
	sema_init(&plasma->limit_sem, WRITES_IN_FLIGHT);
	mutex_init(&plasma->io_mutex);
	spin_lock_init(&plasma->err_lock);
	init_usb_anchor(&plasma->submitted);
	init_waitqueue_head(&plasma->packet_in_wait);

	plasma->udev = usb_get_dev(interface_to_usbdev(interface));
	plasma->interface = interface;

	/* set up the endpoint information */
	/* use only the first interrupt-in and interrupt-out endpoints */
	if ((error = usb_find_common_endpoints(interface->cur_altsetting, NULL, NULL, &packet_in, &packet_out))) {
		dev_err(&interface->dev, "Could not find both interrupt-in and interrupt-out endpoints\n");
		goto error;
	}

	plasma->packet_size = usb_endpoint_maxp(packet_in);
	plasma->packet_in_endpointAddr = packet_in->bEndpointAddress;

	if (NULL == (plasma->packet_buffer = kmalloc(plasma->packet_size, GFP_KERNEL))) {
		error = -ENOMEM;
		goto error;
	}

	if (NULL == (plasma->packet_cache = kmalloc(plasma->packet_size * TOTAL_PACKETS, GFP_KERNEL))) {
		error = -ENOMEM;
		goto error;
	}

	if (NULL == (plasma->packet_in_urb = usb_alloc_urb(0, GFP_KERNEL))) {
		error = -ENOMEM;
		goto error;
	}

	plasma->packet_out_endpointAddr = packet_out->bEndpointAddress;

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, plasma);

	/* we can register the device now, as it is ready */
	if ((error = usb_register_dev(interface, &plasma_class))) {
		dev_err(&interface->dev, "Failed to get a minor for this device: %d.", error);
		goto error;
	}

	/* Create the directory structure */
	if ((error = sysfs_create_groups(&interface->dev.kobj, attr_groups))) {
		dev_err(&interface->dev, "Failed to create filesystem: %d.", error);
		goto error;
	}

	if ((error = initialize_cache(plasma))) {
		dev_err(&interface->dev, "Failed to initialize cache: %d.", error);
		goto error;
	}

	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev, "Plasma device now attached to plasma-%d", interface->minor);

	return 0;
error:
	// sysfs_remove_groups(&interface->dev.kobj, attr_groups);
	usb_set_intfdata(interface, NULL);
	kref_put(&plasma->kref, plasma_delete);

	return error;
}

static void plasma_disconnect(struct usb_interface *interface)
{
	struct usb_plasma *plasma;
	int minor = interface->minor;

	sysfs_remove_groups(&interface->dev.kobj, attr_groups);

	plasma = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* give back our minor */
	usb_deregister_dev(interface, &plasma_class);

	/* prevent more I/O from starting */
	mutex_lock(&plasma->io_mutex);
	plasma->interface = NULL;
	mutex_unlock(&plasma->io_mutex);

	usb_kill_anchored_urbs(&plasma->submitted);

	/* decrement our usage count */
	kref_put(&plasma->kref, plasma_delete);

	dev_info(&interface->dev, "plasma-%d now disconnected", minor);
}

static void plasma_draw_down(struct usb_plasma *plasma)
{
	int time;

	time = usb_wait_anchor_empty_timeout(&plasma->submitted, 1000);
	if (!time)
		usb_kill_anchored_urbs(&plasma->submitted);

	usb_kill_urb(plasma->packet_in_urb);
}

static int plasma_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_plasma *plasma = usb_get_intfdata(intf);

	if (plasma)
		plasma_draw_down(plasma);

	return 0;
}

static int plasma_resume(struct usb_interface *intf)
{
	return 0;
}

static int plasma_pre_reset(struct usb_interface *intf)
{
	struct usb_plasma *plasma = usb_get_intfdata(intf);

	mutex_lock(&plasma->io_mutex);
	plasma_draw_down(plasma);

	return 0;
}

static int plasma_post_reset(struct usb_interface *intf)
{
	struct usb_plasma *plasma = usb_get_intfdata(intf);

	/* we are sure no URBs are active - no locking needed */
	plasma->error = -EPIPE;
	mutex_unlock(&plasma->io_mutex);

	return 0;
}

static struct usb_driver plasma_driver = {
	.name =			"plasma",
	.probe =		plasma_probe,
	.disconnect =	plasma_disconnect,
	.suspend =		plasma_suspend,
	.resume =		plasma_resume,
	.pre_reset =	plasma_pre_reset,
	.post_reset =	plasma_post_reset,
	.id_table =		plasma_table,
	.supports_autosuspend = 1,
};

module_usb_driver(plasma_driver);

static ssize_t send_packet(struct usb_plasma *plasma)
{
	int dummy, error;

	dump_packet_selection("packet out: ", plasma);

	error = usb_interrupt_msg(
		plasma->udev,
		usb_sndintpipe(plasma->udev, plasma->packet_out_endpointAddr),
		plasma->packet_selection,
		plasma->packet_size,
		&dummy,
		1000
	);

	if (error) {
		dev_err(&plasma->interface->dev, "write error: %d\n", error);
		goto error;
	}

	error = usb_interrupt_msg(
		plasma->udev,
		usb_rcvintpipe(plasma->udev, plasma->packet_in_endpointAddr),
		plasma->packet_buffer,
		plasma->packet_size,
		&dummy,
		1000
	);

	if (error) {
		dev_err(&plasma->interface->dev, "read error: %d\n", error);
		goto error;
	}

	if (0xFF == plasma->packet_buffer[0] && 0xAA == plasma->packet_buffer[1]) {
		dev_err(&plasma->interface->dev, "USB responded with an error packet.");
		return -1;
	}

	print_hex_dump_bytes("packet in: ", DUMP_PREFIX_NONE, plasma->packet_buffer, plasma->packet_size);

	if (plasma->packet_buffer != plasma->packet_selection)
		memcpy(plasma->packet_selection, plasma->packet_buffer, plasma->packet_size);

	return dummy;
error:
	print_hex_dump_bytes("packet dump: ", DUMP_PREFIX_NONE, plasma->packet_buffer, plasma->packet_size);

	return error;
}

/**********************************************************************
 * Helpers for readin writing the buffer packet
 *********************************************************************/

static int select_packet_cache(struct usb_plasma *plasma, int index)
{
	if (0 > index || TOTAL_PACKETS <= index) {
		dev_err(&plasma->interface->dev, "Failed to send packet cache: index (%d) out of range..", index);
		return -ERANGE;
	}

	plasma->packet_selection = &plasma->packet_cache[index * plasma->packet_size];

	return 0;
}

static int select_packet_buffer(struct usb_plasma *plasma)
{
	if (plasma->error) {
		dev_err(&plasma->interface->dev, "Failed to select packet buffer: Device has an error %d.", plasma->error);
		return plasma->error;
	}

	plasma->packet_selection = plasma->packet_buffer;

	return 0;
}

static int reset_packet_header(struct usb_plasma *plasma, byte mode, byte pkt, byte index, byte flag, byte pad)
{
	if (plasma->error) {
		dev_err(&plasma->interface->dev, "Failed to set packet header: Device has an error %d.", plasma->error);
		return plasma->error;
	}

	memset(plasma->packet_selection, pad, plasma->packet_size);

	plasma->packet_selection[0] = mode;
	plasma->packet_selection[1] = pkt;
	plasma->packet_selection[2] = index;
	plasma->packet_selection[3] = flag;

	return 0;
}

/**********************************************************************
 * Helpers for reading/writing to cached packets
 *********************************************************************/

static int validate_packet_range(struct usb_plasma *plasma, int offset, size_t count)
{
	const int end = offset + count;

	// If there is a problem with the USB we should always fail
	if (0 != plasma->error) {
		dev_err(&plasma->interface->dev, "An unhandled error (%d) is already in place.", plasma->error);
		return plasma->error;
	}

	if (0 > offset || offset >= plasma->packet_size) {
		dev_err(&plasma->interface->dev, "Packet offset (%d) out of range.", offset);
		return -ERANGE;
	}

	if (0 > end || end > plasma->packet_size) {
		dev_err(&plasma->interface->dev, "Packet copy (%d) out of range.", end);
		plasma->error = -ERANGE;
	}

	return 0;
}

static ssize_t read_packet_bytes(struct usb_plasma *plasma, int offset, byte *buffer, size_t count)
{
	if (0 > validate_packet_range(plasma, offset, count))
		return plasma->error;

	memcpy(buffer, &plasma->packet_selection[offset], count);

	return count;
}

static ssize_t write_packet_bytes(struct usb_plasma *plasma, int offset, const byte *buffer, size_t count)
{
	if (0 > validate_packet_range(plasma, offset, count))
		return plasma->error;

	memcpy(&plasma->packet_selection[offset], buffer, count);

	return count;
}

static byte* get_packet_pointer(struct usb_plasma *plasma, int offset)
{
	if (0 <= validate_packet_range(plasma, offset, 0))
		return &plasma->packet_selection[offset];

	return NULL;
}

static byte get_packet_byte(struct usb_plasma *plasma, int offset)
{
	byte value = 0xFF;

	read_packet_bytes(plasma, offset, &value, 1);

	return value;
}

static const char* get_packet_name(int index);

static ssize_t set_packet_byte(struct usb_plasma *plasma, int offset, byte value)
{
	int idx;

	if (plasma->packet_selection == plasma->packet_buffer)
		idx = -1;
	else
		idx = (plasma->packet_selection - plasma->packet_cache) / plasma->packet_size;

	dev_dbg(
		&plasma->interface->dev,
		"Setting byte %d of packet %s to 0x%02x",
		offset,
		get_packet_name(idx),
		value
	 );

	return write_packet_bytes(plasma, offset, &value, 1);
}

static word get_packet_word(struct usb_plasma *plasma, int offset)
{
	word value = 0xFFFF;

	if (0 >= read_packet_bytes(plasma, offset, (byte*)&value, 2))
		return NEEDS_SWAP() ? WORD_SWAP(value) : value;

	return value;
}

// static ssize_t set_packet_word(struct usb_plasma *plasma, int offset, word value)
// {
// 	if (NEEDS_SWAP())
// 		value = WORD_SWAP(value);
//
// 	return write_packet_bytes(plasma, offset, (byte*)&value, 2);
// }

/**********************************************************************
 * Helpers for reading/writing to cached packets
 *********************************************************************/

static const char *MorseCodes[] = {
	" ",		//	<space>  0X20
	"-.-.--",	//	! 0x21
	".-..-.",	//	" 0x22
	"",			//  # 0x23
	"...-..-",	//	$ 0x24
	"",			//  % 0x25
	".-...",	//	& 0x26
	".----.",	//	' 0x27
	"-.--.",	//	( 0x28
	"-.--.-",	//	) 0x29
	"",			//  * 0x25
	".-.-.",	//	+ 0x2B
	"--..--",	//	, 0x2C
	"-....-",	//	- 0x2D
	".-.-.-",	//	. 0x2E
	"-..-.",	//	/ 0x2F

	"-----",	//	0 0x30
	".----",	//	1 0x31
	"..---",	//	2 0x32
	"...--",	//	3 0x33
	"....-",	//	4 0x34
	".....",	//	5 0x35
	"-....",	//	6 0x36
	"--...",	//	7 0x37
	"---..",	//	8 0x38
	"----.",	//	9 0x39
	"---...",	//	: 0x3A
	"-.-.-.",	//	; 0x3B
	"",			//	< 0x3C
	"-...-",	//	= 0x3D
	"",			//	> 0x3E
	"..--..",	//	? 0x3F

	".--.-.",	//	@ 0x40
	".-",		//	A 0x41
	"-...",		//	B 0x42
	"-.-.",		//	C 0x43
	"-..",		//	D 0x44
	".",		//	E 0x45
	"..-.",		//	F 0x46
	"--.",		//	G 0x47
	"....",		//	H 0x48
	"..",		//	I 0x49
	".---",		//	J 0x4A
	"-.-",		//	K 0x4B
	".-..",		//	L 0x4C
	"--",		//	M 0x4D
	"-.",		//	N 0x4E
	"---",		//	O 0x4F

	".--.",		//	P 0x50
	"--.-",		//	Q 0x51
	".-.",		//	R 0x52
	"...",		//	S 0x53
	"-",		//	T 0x54
	"..-",		//	U 0x55
	"...-",		//	V 0x56
	".--",		//	W 0x57
	"-..-",		//	X 0x58
	"-.--",		//	Y 0x59
	"--..",		//	Z 0x5A
};

static byte *const * get_morse_packets(struct attribute_meta *meta)
{
	int i;
	static byte *address[8];

	for (i = 0; i < 8; ++i) {
		if (0 > (meta->error = select_packet_cache(meta->plasma, IDX_MORSE_FIRST + i)))
			return NULL;

		if (NULL == (address[i] = get_packet_pointer(meta->plasma, 4)))
			return NULL;
	}

	return address;
}

static ssize_t morse_to_ascii(struct usb_plasma *plasma, int slot, char *buf)
{
	// The morse codes span 8 packets, with 2 packets for each code.
	// Packets 0 and 1 are the applied sequence, 2-3 4-5 6-7 are the
	// individual slots. The active sequence is stored twice.

	byte bits;
	const int max = PAGE_SIZE - 1;
	int input, output, shift, i, c;
	const char *pStart, *pEnd;
	const byte *pSource;
	size_t seq_len;
	char letter;

	int code_count = sizeof(MorseCodes) / sizeof(MorseCodes[0]);
	int packet_idx = IDX_MORSE_FIRST + (2 * slot);

	// Convert the binary back to a morse code string
	for (i = 0; i < 2; ++i) {
		if (0 > (plasma->error = select_packet_cache(plasma, packet_idx + i)))
			return plasma->error;

		pSource = get_packet_pointer(plasma, 4);

		for (input = 0, output = 0; input < 60 && output <= max; input++) {
			shift = 0;
			while (shift <= 6) {
				bits = (pSource[input] >> shift) & 0x03;
				shift += 2;
				if (0x00 == bits) {
					buf[output++] = ' ';
				} else if (0x01 == bits) {
					buf[output++] = '.';
				} else if (0x02 == bits) {
					buf[output++] = '-';
				} else if (0x03 == bits) {
					buf[output++] = '\n';
					input = 120;
					i = 120;
					break;
				}
			}
		}
	}

	pStart = buf;

	for (pEnd = strchr(pStart, ' '); NULL != pEnd && output < max; pEnd = strchr(pStart, ' ')) {
		seq_len = pEnd - pStart;
		letter = 0;

		// A double space indicate a new ascii word
		if (0 == seq_len) {
			if (' ' != *(pEnd + 1))
				break;
			letter = ' ';
			pEnd++;
		} else {
			for (i = 0; i < code_count; ++i) {
				for (c = 0; c <= seq_len; ++c) {
					if (0 == MorseCodes[i][c] && ' ' == pStart[c]) {
						letter = 0x20 + i;
						break;
					}
					if (MorseCodes[i][c] != pStart[c])
						break;
				}
				if (0 != letter)
					break;
			}
			if (0 == letter)
				break;
		}

		buf[output++] = letter;
		pStart = pEnd + 1;
	}

	buf[output] = 0;

	return output;
}

static ssize_t ascii_to_morse(struct usb_plasma *plasma, byte *const *pPackets, const char *buf, size_t count)
{
	// There are a total of 120 bytes available, with 4 morse points to each byte.
	int iter, idx;
	const char *pChar;
	byte *pOut = pPackets[0];

	// byte bin[120] = {0}; // TODO - Write directly to packet
	byte currbyte = 0;
	// int packet_idx = IDX_MORSE_FIRST + (2 * slot);
	int shift = 0;
	int output = 0;

	// pPacket[0] = &plasma->packet_selection[(IDX_MORSE_FIRST + index) * plasma->packet_size][4];
	// pPacket[1] = &plasma->packet_selection[(IDX_MORSE_FIRST + index) * plasma->packet_size][4];

	for (iter = 0; iter < count && output <= 120 && buf[iter] != 0; ++iter) {
		idx = buf[iter];

		if (output == 60)
			pOut = pPackets[1];

		if (0x61 <= idx && 0x7A >= idx)
			idx -= 0x20;
		else if (0x20 > idx || 0x5A < idx)
			continue;

		for (pChar = MorseCodes[idx - 0x20]; *pChar; ++pChar) {
			if (*pChar == '.')
				currbyte |= (0x01 << shift);
			else if (*pChar == '-')
				currbyte |= (0x02 << shift);
			else if (*pChar != ' ')
				continue;

			if (6 == shift) {
				shift = 0;
				pOut[output] = currbyte;
				currbyte = 0;
				output++;
			} else {
				shift += 2;
			}
		}

		// Add a space as 0x00 after each sequence
		if (6 == shift) {
			shift = 0;
			pOut[output] = currbyte;
			currbyte = 0;
			output++;
		} else {
			shift += 2;
		}
	}

	currbyte |= (0x03 << shift);
	pOut[output] = currbyte;

	// for (i = 0; i < 2; ++i) {
	// 	if (0 > (error = select_packet_cache(plasma, packet_idx + i)))
	// 		return error;
	//
	// 	if (0 > (error = write_packet_bytes(plasma, 4, &bin[60 * i], 60)))
	// 		return error;
	// }

	return count;
}

static size_t read_number(const char *pStr, unsigned int *pResult)
{
	byte c;
	int i;

	*pResult = 0;

	if ('0' == pStr[0] && ('x' == pStr[1] || 'X' == pStr[1])) {
		for (i = 2; i < 10; ++i) {
			if (0x0F <= (c = TO_HEX(pStr[i])))
				*pResult = (*pResult << 4) | c;
			else
				break;
		}
	} else {
		for (i = 0; '0' <= pStr[i] && '9' >= pStr[i]; ++i)
			*pResult = (*pResult * 10) + (pStr[i] - '0');
	}

	return i;
}

static bool read_string(const char *pInput, const char *pMatch)
{
	return 0 == strncasecmp(pInput, pMatch, strlen(pMatch));
}

static const char* get_packet_name(int index)
{
	switch (index) {
		case IDX_PACKET_BUFFER:
			return "buffer";
		case IDX_EFFECT_STATIC:
			return EFFECT_NAME_STATIC;
		case IDX_EFFECT_RAINBOW:
			return EFFECT_NAME_RAINBOW;
		case IDX_EFFECT_SWIRL:
			return EFFECT_NAME_SWIRL;
		case IDX_EFFECT_CHASE:
			return EFFECT_NAME_CHASE;
		case IDX_EFFECT_BOUNCE:
			return EFFECT_NAME_BOUNCE;
		case IDX_EFFECT_MORSE:
			return EFFECT_NAME_MORSE;
		case IDX_EFFECT_CYCLE:
			return EFFECT_NAME_CYCLE;
		case IDX_EFFECT_BREATH:
			return EFFECT_NAME_BREATH;
		case IDX_ZONE_LOGO:
			return "logo";
		case IDX_ZONE_FAN:
			return "fan";
		case IDX_ACTIVE_ZONES:
			return "active";
		case IDX_MORSE_FIRST:
			return "morse_page_0";
		case IDX_MORSE_FIRST + 1:
			return "morse_page_1";
		case IDX_MORSE_FIRST + 2:
			return "morse_page_2";
		case IDX_MORSE_FIRST + 3:
			return "morse_page_3";
		case IDX_MORSE_FIRST + 4:
			return "morse_page_4";
		case IDX_MORSE_FIRST + 5:
			return "morse_page_5";
		case IDX_MORSE_FIRST + 6:
			return "morse_page_6";
		case IDX_MORSE_FIRST + 7:
			return "morse_page_7";
		case IDX_FREQUENCY:
			return "frequency";
		case IDX_RESOLUTION:
			return "resolution";
	}

	return "<invalid>";
}

static int get_ring_effect_id(struct usb_plasma *plasma)
{
	const byte *pPacket = &plasma->packet_cache[plasma->packet_size * IDX_ACTIVE_ZONES];

	return pPacket[OFF_PKT_ACTIVE_ZONES_RING_ZONE];
}

static int get_ring_effect_index(struct usb_plasma *plasma)
{
	const int id = get_ring_effect_id(plasma);

	if (EFFECT_ID_STATIC == id)
		return IDX_EFFECT_STATIC;
	if (EFFECT_ID_RAINBOW == id)
		return IDX_EFFECT_RAINBOW;
	if (EFFECT_ID_SWIRL == id)
		return IDX_EFFECT_SWIRL;
	if (EFFECT_ID_CHASE == id)
		return IDX_EFFECT_CHASE;
	if (EFFECT_ID_BOUNCE == id)
		return IDX_EFFECT_BOUNCE;
	if (EFFECT_ID_MORSE == id)
		return IDX_EFFECT_MORSE;
	if (EFFECT_ID_CYCLE == id)
		return IDX_EFFECT_CYCLE;
	if (EFFECT_ID_BREATH == id)
		return IDX_EFFECT_BREATH;

	return -1;
}

static int initialize_cache(struct usb_plasma *plasma)
{
	int error, i;

	// Read the settings for each effect
	for (i = 0; i < sizeof(EffectMap); i++) {
		if (0 > (error = select_packet_cache(plasma, i)))
			return error;
		if (0 > (error = reset_packet_header(plasma, MODE_PROFILE|MODE_READ, PKT_EFFECT, 0x01, 0x00, 0xff)))
			return error;
		if (0 > (error = set_packet_byte(plasma, 4, EffectMap[i])))
			return error;
		if (0 > (error = send_packet(plasma)))
			return error;
	}

	// Read the active zones (NOTE zones 5 and 6 are always active)
	if (0 > (error = select_packet_cache(plasma, IDX_ACTIVE_ZONES)))
		return error;
	if (0 > (error = reset_packet_header(plasma, MODE_PROFILE|MODE_READ, PKT_ACTIVE_ZONES, 0x01, 0x00, 0x00)))
		return error;
	if (0 > (error = set_packet_byte(plasma, OFF_PKT_ACTIVE_ZONES_LENGTH, ZONE_LENGTH)))
		return error;
	if (0 > (error = send_packet(plasma)))
		return error;

	// Read the fan settings
	if (0 > (error = select_packet_cache(plasma, IDX_ZONE_FAN)))
		return error;
	if (0 > (error = reset_packet_header(plasma, MODE_PROFILE|MODE_READ, PKT_EFFECT, 0x01, 0x00, 0xFF)))
		return error;
	if (0 > (error = set_packet_byte(plasma, 4, ZONE_ID_FAN)))
		return error;
	if (0 > (error = send_packet(plasma)))
		return error;

	// Read the logo settings
	if (0 > (error = select_packet_cache(plasma, IDX_ZONE_LOGO)))
		return error;
	if (0 > (error = reset_packet_header(plasma, MODE_PROFILE|MODE_READ, PKT_EFFECT, 0x01, 0x00, 0xFF)))
		return error;
	if (0 > (error = set_packet_byte(plasma, 4, ZONE_ID_LOGO)))
		return error;
	if (0 > (error = send_packet(plasma)))
		return error;

	// Read the morse pages
	for (i = 0; i < MORSE_LENGTH; i++) {
		if (0 > (error = select_packet_cache(plasma, IDX_MORSE_FIRST + i)))
			return error;
		if (0 > (error = reset_packet_header(plasma, MODE_PROFILE|MODE_READ, PKT_MORSE, i, 0x00, 0x00)))
			return error;
		if (0 > (error = send_packet(plasma)))
			return error;
	}

	// Read the mirage settings
	if (0 > (error = select_packet_cache(plasma, IDX_FREQUENCY)))
		return error;
	if (0 > (error = reset_packet_header(plasma, MODE_PROFILE|MODE_READ, PKT_FREQUENCY, 0x00, 0x00, 0x00)))
		return error;
	if (0 > (error = send_packet(plasma)))
		return error;

	return 0;
}

static int flush_morse_packets(struct attribute_meta *meta)
{
	int i, error;

	struct usb_plasma *plasma = meta->plasma;

	for (i = 0; i < 8; ++i) {
		if (0 > (error = select_packet_cache(plasma, IDX_MORSE_FIRST + i)))
			goto error;
		if (0 > (error = set_packet_byte(plasma, 0, MODE_PROFILE|MODE_WRITE)))
			goto error;
		if (0 > (error = send_packet(plasma)))
			goto error;
	}

	meta->flush_morse = 0;

	dev_dbg(&plasma->interface->dev, "Sent morse stream success.");

	return 0;
error:
	dev_dbg(&plasma->interface->dev, "Failed to send morse packets: (%d).", error);

	return error;
}

static int flush_mirage_packets(struct attribute_meta *meta)
{
	int error;

	struct usb_plasma *plasma = meta->plasma;

	if (0 > (error = select_packet_cache(plasma, IDX_RESOLUTION)))
		goto error;
	if (0 > (error = set_packet_byte(plasma, 0, MODE_PROFILE|MODE_WRITE)))
		goto error;
	if (0 > (error = send_packet(plasma)))
		goto error;

	if (0 > (error = select_packet_cache(plasma, IDX_FREQUENCY)))
		goto error;
	if (0 > (error = set_packet_byte(plasma, 0, MODE_PROFILE|MODE_WRITE)))
		goto error;
	if (0 > (error = send_packet(plasma)))
		goto error;

	meta->flush_mirage = 0;

	dev_dbg(&plasma->interface->dev, "Sent mirage packets.");

	return 0;
error:
	dev_dbg(&plasma->interface->dev, "Failed to send mirage packets: (%d).", error);

	return error;
}

static int flush_effect_packets(struct attribute_meta *meta)
{
	int error, ring_index;

	struct usb_plasma *plasma = meta->plasma;

	if (0 > (error = select_packet_buffer(plasma)))
		goto error;

	if (0 > (error = reset_packet_header(plasma, MODE_CONTROL|MODE_WRITE, 0x80, 0x00, 0x00, 0x00)))
		goto error;
	if (0 > (error = send_packet(plasma)))
		goto error;

	if (0 > (error = reset_packet_header(plasma, MODE_PROFILE|MODE_WRITE, 0x96, 0x00, 0x00, 0x00)))
		goto error;
	if (0 > (error = send_packet(plasma)))
		goto error;

	if (0 > (error = reset_packet_header(plasma, MODE_PROFILE|MODE_WRITE, 0x28, 0x00, 0x00, 0x00)))
		goto error;
	if (0 > (error = set_packet_byte(plasma, 4, 0xE0)))
		goto error;
	if (0 > (error = send_packet(plasma)))
		goto error;

	if (meta->flush_mirage) {
		if (0 > (error = flush_mirage_packets(meta)))
			goto error;
	}

	// Send the fan zone settings
	if (0 > (error = select_packet_cache(plasma, IDX_ZONE_FAN)))
		goto error;
	if (0 > (error = set_packet_byte(plasma, 0, MODE_PROFILE|MODE_WRITE)))
		goto error;
	if (0 > (error = send_packet(plasma)))
		goto error;

	if (0 > (error = select_packet_cache(plasma, IDX_ZONE_LOGO)))
		goto error;
	if (0 > (error = set_packet_byte(plasma, 0, MODE_PROFILE|MODE_WRITE)))
		goto error;
	if (0 > (error = send_packet(plasma)))
		goto error;

	// TODO only the modified packets need sending
	if (meta->flush_morse) {
		if (0 > (error = flush_morse_packets(meta)))
			goto error;
	}

	if (0 <= (ring_index = get_ring_effect_index(plasma))) {
		if (0 > (error = select_packet_cache(plasma, ring_index)))
			goto error;
		if (0 > (error = set_packet_byte(plasma, 0, MODE_PROFILE|MODE_WRITE)))
			goto error;
		if (0 > (error = send_packet(plasma)))
			goto error;
	}

	if (0 > (error = select_packet_cache(plasma, IDX_ACTIVE_ZONES)))
		goto error;
	if (0 > (error = set_packet_byte(plasma, 0, MODE_PROFILE|MODE_WRITE)))
		goto error;
	if (0 > (error = send_packet(plasma)))
		goto error;

	// Send the end of sequence packet
	if (0 > (error = select_packet_buffer(plasma)))
		goto error;

	if (0 > (error = reset_packet_header(plasma, MODE_PROFILE|MODE_WRITE, 0x28, 0x00, 0x00, 0x00)))
		goto error;
	if (0 > (error = set_packet_byte(plasma, 4, 0xE0)))
		goto error;
	if (0 > (error = send_packet(plasma)))
		goto error;

	meta->flush_profile = 0;

	dev_dbg(&plasma->interface->dev, "Sent effect packets.");

	return 0;
error:
	dev_dbg(&plasma->interface->dev, "Failed to send effect packets: (%d).", error);

	return error;
}

/**********************************************************************
 * Reading and writing of settings
 *********************************************************************/

static struct attribute_meta get_attribute_meta(struct device *dev, struct device_attribute *attr)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct attribute *const *iter;
	int i;

	struct attribute_meta meta = {
		.error = 0,
		.plasma = usb_get_intfdata(interface),
		.zone_name = NULL,
		.attr_name = attr->attr.name,
		.attr_slot = 0,
		.is_ring = false,
		.flush_profile = 0,
	};

	if (!meta.plasma) {
		meta.error = -EBADFD;
		return meta;
	}

	if (meta.plasma->error) {
		meta.error = meta.plasma->error;
		return meta;
	}

	for (i = 0; meta.zone_name == NULL && attr_groups[i]; i++) {
		for (iter = attr_groups[i]->attrs; *iter; iter++) {
			if (*iter == &attr->attr) {
				meta.zone_name = attr_groups[i]->name;
				break;
			}
		}
	}

	if (0 == strcmp(meta.zone_name, "logo")) {
		meta.effect_index = IDX_ZONE_LOGO;
	} else if (0 == strcmp(meta.zone_name, "fan")) {
		meta.effect_index = IDX_ZONE_FAN;
	} else if (0 == strcmp(meta.zone_name, "ring")) {
		meta.effect_index = get_ring_effect_index(meta.plasma);
		meta.is_ring = true;
	} else {
		dev_err(&meta.plasma->interface->dev, "Unrecognized attribute \"%s\".", meta.zone_name);
		meta.error = -ERR_UNEXPECTED_VALUE;
		return meta;
	}

	if (0 == strcmp(meta.attr_name, "enabled")) {
		meta.attr_id = ATTR_ENABLED;
	} else if (0 == strcmp(meta.attr_name, "effect")) {
		meta.attr_id = ATTR_EFFECT;
	} else if (0 == strcmp(meta.attr_name, "color")) {
		meta.attr_id = ATTR_COLOR;
	} else if (0 == strcmp(meta.attr_name, "random")) {
		meta.attr_id = ATTR_RANDOM;
	} else if (0 == strcmp(meta.attr_name, "brightness")) {
		meta.attr_id = ATTR_BRIGHTNESS;
	} else if (0 == strcmp(meta.attr_name, "speed")) {
		meta.attr_id = ATTR_SPEED;
	} else if (0 == strcmp(meta.attr_name, "direction")) {
		meta.attr_id = ATTR_DIRECTION;
	} else if (0 == strcmp(meta.attr_name, "repeat")) {
		meta.attr_id = ATTR_REPEAT;
	} else if (0 == strncmp(meta.attr_name, "mirage", 6)) {
		if (0 == strcmp(meta.attr_name, "mirage_slot1")) {
			meta.attr_slot = 1;
		} else if (0 == strcmp(meta.attr_name, "mirage_slot2")) {
			meta.attr_slot = 2;
		} else if (0 == strcmp(meta.attr_name, "mirage_slot3")) {
			meta.attr_slot = 3;
		}
		meta.attr_id = ATTR_MIRAGE;
	} else if (0 == strncmp(meta.attr_name, "morse", 5)) {
		if (0 == strcmp(meta.attr_name, "morse_slot1")) {
			meta.attr_slot = 1;
		} else if (0 == strcmp(meta.attr_name, "morse_slot2")) {
			meta.attr_slot = 2;
		} else if (0 == strcmp(meta.attr_name, "morse_slot3")) {
			meta.attr_slot = 3;
		}
		meta.attr_id = ATTR_MORSE;
	} else {
		dev_err(&meta.plasma->interface->dev, "Unrecognized attribute \"%s\".", meta.attr_name);
		meta.error = -ERR_UNEXPECTED_VALUE;
		return meta;
	}

	if (meta.is_ring) {
		// NOTE This presumes each LED has the same effect
		meta.effect_id = get_ring_effect_id(meta.plasma);
		meta.zone_enabled = 0xFE != meta.effect_id;
	} else {
		// The effect id is stored in p3 for the fan/logo.
		if (0 > (meta.error = select_packet_cache(meta.plasma, meta.effect_index)))
			return meta;

		meta.effect_id = get_packet_byte(meta.plasma, OFF_PKT_EFFECT_P3);
		meta.zone_enabled = 0x00 != meta.effect_id;

		if (meta.zone_enabled) {
			if (1 == meta.effect_id) {
				meta.effect_id = EFFECT_ID_STATIC;
			} else if (2 == meta.effect_id) {
				meta.effect_id = EFFECT_ID_CYCLE;
			} else if (3 == meta.effect_id) {
				meta.effect_id = EFFECT_ID_BREATH;
			} else {
				dev_err(&meta.plasma->interface->dev, "Not a valid effect ID: 0x%02X.", meta.effect_id);
				dump_packet_index("", meta.plasma, meta.effect_index);
				meta.error = -ERR_UNEXPECTED_VALUE;
			}
		}
	}

	return meta;
}

static ssize_t get_setting_effect(struct attribute_meta *meta, char *buf)
{
	const char * effect_name = "null";

	if (!meta->zone_enabled) {
		meta->error = -ERR_ATTR_NOT_AVAILABLE;
	} else if (EFFECT_ID_STATIC == meta->effect_id) {
		effect_name = EFFECT_NAME_STATIC;
	} else if (EFFECT_ID_RAINBOW == meta->effect_id) {
		effect_name = EFFECT_NAME_RAINBOW;
	} else if (EFFECT_ID_SWIRL == meta->effect_id) {
		effect_name = EFFECT_NAME_SWIRL;
	} else if (EFFECT_ID_CHASE == meta->effect_id) {
		effect_name = EFFECT_NAME_CHASE;
	} else if (EFFECT_ID_BOUNCE == meta->effect_id) {
		effect_name = EFFECT_NAME_BOUNCE;
	} else if (EFFECT_ID_MORSE == meta->effect_id) {
		effect_name = EFFECT_NAME_MORSE;
	} else if (EFFECT_ID_CYCLE == meta->effect_id) {
		effect_name = EFFECT_NAME_CYCLE;
	} else if (EFFECT_ID_BREATH == meta->effect_id) {
		effect_name = EFFECT_NAME_BREATH;
	} else {
		meta->error = -ERR_UNEXPECTED_VALUE;
	}

	return sprintf(buf, effect_name);
}

static ssize_t put_setting_effect(struct attribute_meta *meta, const char *buf, size_t count)
{
	int index, i;
	byte value;
	const byte *pData;

	// Cycle and static require a p3 change when not in ring
	// Only 11 bytes from p1 need copying.
	// Setting an effect should also enable.
	if (!meta->is_ring) {
		if (read_string(buf, EFFECT_NAME_STATIC)) {
			index = IDX_EFFECT_STATIC;
			value = 0x01;
		} else if (read_string(buf, EFFECT_NAME_CYCLE)) {
			index = IDX_EFFECT_CYCLE;
			value = 0x02;
		} else if (read_string(buf, EFFECT_NAME_BREATH)) {
			index = IDX_EFFECT_BREATH;
			value = 0x03;
		} else {
			return meta->error = -ERR_EFFECT_NOT_SUPPORTED;
		}

		// Copy the bytes then change p3
		if (0 > (meta->error = select_packet_cache(meta->plasma, index)))
			return meta->error;
		if (NULL == (pData = get_packet_pointer(meta->plasma, OFF_PKT_EFFECT_P1)))
			return meta->error = -ERR_UNEXPECTED_VALUE;

		if (0 > (meta->error = select_packet_cache(meta->plasma, meta->effect_index)))
			return meta->error;
		if (0 > (meta->error = write_packet_bytes(meta->plasma, OFF_PKT_EFFECT_P1, pData, 11)))
			return meta->error;
		if (0 > (meta->error = set_packet_byte(meta->plasma, OFF_PKT_EFFECT_P3, value)))
			return meta->error;
	} else {
		if (read_string(buf, EFFECT_NAME_STATIC)) {
			value = EFFECT_ID_STATIC;
		} else if (read_string(buf, EFFECT_NAME_RAINBOW)) {
			value = EFFECT_ID_RAINBOW;
		} else if (read_string(buf, EFFECT_NAME_SWIRL)) {
			value = EFFECT_ID_SWIRL;
		} else if (read_string(buf, EFFECT_NAME_CHASE)) {
			value = EFFECT_ID_CHASE;
		} else if (read_string(buf, EFFECT_NAME_BOUNCE)) {
			value = EFFECT_ID_BOUNCE;
		} else if (read_string(buf, EFFECT_NAME_MORSE)) {
			value = EFFECT_ID_MORSE;
		} else if (read_string(buf, EFFECT_NAME_CYCLE)) {
			value = EFFECT_ID_CYCLE;
		} else if (read_string(buf, EFFECT_NAME_BREATH)) {
			value = EFFECT_ID_BREATH;
		} else {
			return meta->error = -ERR_UNEXPECTED_VALUE;
		}

		if (0 > (meta->error = select_packet_cache(meta->plasma, IDX_ACTIVE_ZONES)))
			return meta->error;

		for (i = 0; i < 15; ++i) {
			if (0 > (meta->error = set_packet_byte(meta->plasma, OFF_PKT_ACTIVE_ZONES_RING_ZONE + i, value)))
				return meta->error;
		}
	}

	meta->flush_profile++;

	return count;
}

static ssize_t get_setting_enabled(struct attribute_meta *meta, char *buf)
{
	return sprintf(buf, "%s", meta->zone_enabled ? "1" : "0");
}

static ssize_t put_setting_enabled(struct attribute_meta *meta, const char *buf, size_t count)
{
	int i;

	if (0 == strncmp(buf, "0", 1) || 0 == strncasecmp(buf, "off", 3) || 0 == strncasecmp(buf, "false", 5)) {
		if (meta->is_ring) {
			// The ring has 15 LEDS, each of which has to be set.
			// TODO How can we apply a differnt color to each LED?
			dev_dbg(&meta->plasma->interface->dev, "Disabling Ring.");
			if (0 > (meta->error = select_packet_cache(meta->plasma, IDX_ACTIVE_ZONES)))
				return meta->error;
			for (i = 0; i < 15; ++i) {
				if (0 > (meta->error = set_packet_byte(meta->plasma, OFF_PKT_ACTIVE_ZONES_RING_ZONE + i, 0xFE)))
				return meta->error;
			}
		} else {
			dev_dbg(&meta->plasma->interface->dev, "Disabling Zone: %d.", meta->effect_index);
			if (0 > (meta->error = select_packet_cache(meta->plasma, meta->effect_index)))
				return meta->error;
			if (0 > (meta->error = set_packet_byte(meta->plasma, OFF_PKT_EFFECT_P3, 0x00)))
				return meta->error;
		}
	} else {
		dev_dbg(&meta->plasma->interface->dev, "Enabling with static effect.");
		put_setting_effect(meta, "static", 6);
	}

	meta->flush_profile++;

	return count;
}

static ssize_t get_setting_color(struct attribute_meta *meta, char *buf)
{
	byte rgb[3];

	if (EFFECT_ID_RAINBOW != meta->effect_id
		&& EFFECT_ID_BOUNCE != meta->effect_id
		&& EFFECT_ID_CYCLE != meta->effect_id)
	{
		if (0 > (meta->error = select_packet_cache(meta->plasma, meta->effect_index)))
			return sprintf(buf, "error");

		if (!meta->zone_enabled) {
			meta->error = -ERR_ATTR_NOT_AVAILABLE;
		} else if (0 > (meta->error = read_packet_bytes(meta->plasma, OFF_PKT_EFFECT_R, rgb, 3))) {
			return sprintf(buf, "error");
		} else {
			return sprintf(buf, "r=0x%02X g=0x%02X b=0x%02X", rgb[0], rgb[1], rgb[2]);
		}
	} else {
		meta->error = -ERR_ATTR_NOT_SUPPORTED;
	}

	return sprintf(buf, "null");
}

static ssize_t put_setting_color(struct attribute_meta *meta, const char *buf, size_t count)
{
	int shift, i;
	byte h, l;
	unsigned int hex = 0;


	if (EFFECT_ID_RAINBOW != meta->effect_id
		&& EFFECT_ID_BOUNCE != meta->effect_id
		&& EFFECT_ID_CYCLE != meta->effect_id)
	{
		if (!meta->zone_enabled)
			return meta->error = -ERR_ATTR_NOT_AVAILABLE;

		// Should allow "r=0xHH g=0xHH b=0xHH", "(0x)?HHHHHH(HH)?"
		// We should allow multiple formats for the input
		for (shift = 0, h = 0xFF, i = 0; i < count && shift < 4; ++i) {
			if (0x0F >= (l = TO_HEX(buf[i]))) {
				if (0xFF == h) {
					h = l;
				} else {
					// Found a hex pair
					hex = (hex << 8) | (h << 4) | l;
					shift++;
					h = 0xFF;
				}
			} else {
				h = 0xFF;
			}
		}

		dev_dbg(&meta->plasma->interface->dev, "Found %d hex pairs (0x%08X)", shift, hex);

		if (3 <= shift) {
			dev_dbg(&meta->plasma->interface->dev, "Setting color: r=0x%02X g=0x%02X b=0x%02X", (hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);

			if (0 > (meta->error = select_packet_cache(meta->plasma, meta->effect_index)))
				return meta->error;

			// The low 3 bytes should be RGB
			if (0 > (meta->error = set_packet_byte(meta->plasma, OFF_PKT_EFFECT_R, (hex >> 16) & 0xFF)))
				return meta->error;
			if (0 > (meta->error = set_packet_byte(meta->plasma, OFF_PKT_EFFECT_G, (hex >> 8) & 0xFF)))
				return meta->error;
			if (0 > (meta->error = set_packet_byte(meta->plasma, OFF_PKT_EFFECT_B, hex & 0xFF)))
				return meta->error;

			meta->flush_profile++;

			return count;
		} else {
			return meta->error = -ERR_INVALID_INPUT;
		}
	} else {
		return meta->error = -ERR_ATTR_NOT_SUPPORTED;
	}

	return 0;
}

static ssize_t get_setting_random(struct attribute_meta *meta, char *buf)
{
	byte value;

	if (EFFECT_ID_SWIRL != meta->effect_id
		&& EFFECT_ID_CHASE != meta->effect_id
		&& EFFECT_ID_MORSE != meta->effect_id
		&& EFFECT_ID_BREATH != meta->effect_id) {
		meta->error = -ERR_ATTR_NOT_SUPPORTED;
		return sprintf(buf, "null");
	}

	if (!meta->zone_enabled) {
		meta->error = -ERR_ATTR_NOT_AVAILABLE;
		return sprintf(buf, "null");
	}

	if (0 > (meta->error = select_packet_cache(meta->plasma, meta->effect_index)))
		return sprintf(buf, "error");

	// If 0x80 bit is set on p2 random colors are enabled
	value = get_packet_byte(meta->plasma, OFF_PKT_EFFECT_P2);

	if ((value & 0x80) == 0)
		return sprintf(buf, "0");

	return sprintf(buf, "1");
}

static ssize_t put_setting_random(struct attribute_meta *meta, const char *buf, size_t count)
{
	byte value;
	int toggle;

	if (EFFECT_ID_SWIRL != meta->effect_id
		&& EFFECT_ID_CHASE != meta->effect_id
		&& EFFECT_ID_MORSE != meta->effect_id
		&& EFFECT_ID_BREATH != meta->effect_id)
		return meta->error = -ERR_ATTR_NOT_SUPPORTED;

	if (!meta->zone_enabled)
		return meta->error = -ERR_ATTR_NOT_AVAILABLE;

	if (0 > (meta->error = select_packet_cache(meta->plasma, meta->effect_index)))
		return meta->error;

	if (0 == strncmp(buf, "0", 1) || 0 == strncasecmp(buf, "off", 3) || 0 == strncasecmp(buf, "false", 5))
		toggle = 0x00;
	else
		toggle = 0x80;

	value = get_packet_byte(meta->plasma, OFF_PKT_EFFECT_P2) & ~0x80;

	if (0 > (meta->error = set_packet_byte(meta->plasma, OFF_PKT_EFFECT_P2, value | toggle)))
		return meta->error;

	meta->flush_profile++;

	return count;
}

static ssize_t get_setting_brightness(struct attribute_meta *meta, char *buf)
{
	byte value;

	// # Used by all except Cycle
	// 		1: 0x4C, 2: 0x99, 3: 0xFF
	// # Used by Cycle
	// 		1: 0x10, 2: 0x40, 3: 0x7F
	if (meta->zone_enabled) {
		if (0 > (meta->error = select_packet_cache(meta->plasma, meta->effect_index)))
			return sprintf(buf, "error");

		value = get_packet_byte(meta->plasma, OFF_PKT_EFFECT_P5);
		if (EFFECT_ID_CYCLE == meta->effect_id) {
			if (0x10 >= value) {
				value = 1;
			} else if (0x40 >= value) {
				value = 2;
			} else {
				value = 3;
			}
		} else if (0x4C >= value) {
			value = 1;
		} else if (0x99 >= value) {
			value = 2;
		} else {
			value = 3;
		}
	} else {
		meta->error = -ERR_ATTR_NOT_AVAILABLE;
		return sprintf(buf, "null");
	}

	return sprintf(buf, "%d", value);
}

static ssize_t put_setting_brightness(struct attribute_meta *meta, const char *buf, size_t count)
{
	int i;
	byte value;
	const char *pData;

	// # Used by all except Cycle
	// 		1: 0x4C, 2: 0x99, 3: 0xFF
	// # Used by Cycle
	// 		1: 0x10, 2: 0x40, 3: 0x7F
	if (!meta->zone_enabled)
		return meta->error = -ERR_ATTR_NOT_AVAILABLE;

	if (1 != sscanf(buf, "%1d", &i))
		return meta->error = -ERR_INVALID_INPUT;

	value = min(3, max(1, i));

	if (EFFECT_ID_CYCLE == meta->effect_id)
		pData = "\x10\x40\x7F";
	else
		pData = "\x4C\x99\xFF";

	if (0 > (meta->error = select_packet_cache(meta->plasma, meta->effect_index)))
		return meta->error;

	if (0 > (meta->error = set_packet_byte(meta->plasma, OFF_PKT_EFFECT_P5, pData[value - 1])))
		return meta->error;

	meta->flush_profile++;

	return count;

}

static ssize_t get_setting_speed(struct attribute_meta *meta, char *buf)
{
	byte value;

	// # Used by Swirl, Chase, Bounce
	// 		1: 0x77, 2: 0x74, 3: 0x6E, 4: 0x6B, 5: 0x67
	// # Used by Rainbow
	// 		1: 0x72, 2: 0x68, 3: 0x64, 4: 0x62, 5: 0x61
	// # Used by Breathing
	// 		1: 0x3C, 2: 0x37, 3: 0x31, 4: 0x2C, 5: 0x26
	// # Used by Cycle
	// 		1: 0x96, 2: 0x8C, 3: 0x80, 4: 0x6E, 5: 0x68
	if (EFFECT_ID_STATIC == meta->effect_id || EFFECT_ID_MORSE == meta->effect_id) {
		meta->error = -ERR_ATTR_NOT_SUPPORTED;
		return sprintf(buf, "null");
	}

	if (!meta->zone_enabled) {
		meta->error = -ERR_ATTR_NOT_AVAILABLE;
		return sprintf(buf, "null");
	}

	if (0 > (meta->error = select_packet_cache(meta->plasma, meta->effect_index)))
		return sprintf(buf, "error");

	value = get_packet_byte(meta->plasma, OFF_PKT_EFFECT_P1);

	if (EFFECT_ID_CYCLE == meta->effect_id) {
		if (0x96 <= value) value = 1;
		else if (0x8C <= value) value = 2;
		else if (0x80 <= value) value = 3;
		else if (0x6E <= value) value = 4;
		else value = 5;
	} else if (EFFECT_ID_BREATH == meta->effect_id) {
		if (0x3C <= value) value = 1;
		else if (0x37 <= value) value = 2;
		else if (0x31 <= value) value = 3;
		else if (0x2C <= value) value = 4;
		else value = 5;
	} else if (EFFECT_ID_RAINBOW == meta->effect_id) {
		if (0x72 <= value) value = 1;
		else if (0x68 <= value) value = 2;
		else if (0x64 <= value) value = 3;
		else if (0x62 <= value) value = 4;
		else value = 5;
	} else {
		if (0x77 <= value) value = 1;
		else if (0x74 <= value) value = 2;
		else if (0x6E <= value) value = 3;
		else if (0x6B <= value) value = 4;
		else value = 5;
	}

	return sprintf(buf, "%d", value);
}

static ssize_t put_setting_speed(struct attribute_meta *meta, const char *buf, size_t count)
{
	// # Used by Swirl, Chase, Bounce
	// 		1: 0x77, 2: 0x74, 3: 0x6E, 4: 0x6B, 5: 0x67
	// # Used by Rainbow
	// 		1: 0x72, 2: 0x68, 3: 0x64, 4: 0x62, 5: 0x61
	// # Used by Breathing
	// 		1: 0x3C, 2: 0x37, 3: 0x31, 4: 0x2C, 5: 0x26
	// # Used by Cycle
	// 		1: 0x96, 2: 0x8C, 3: 0x80, 4: 0x6E, 5: 0x68
	// Using any other values will not have the same result

	int index, i;
	const char *pData;

	if (EFFECT_ID_STATIC == meta->effect_id || EFFECT_ID_MORSE == meta->effect_id)
		return meta->error = -ERR_ATTR_NOT_SUPPORTED;

	if (!meta->zone_enabled)
		return meta->error = -ERR_ATTR_NOT_AVAILABLE;

	if (1 != sscanf(buf, "%1d", &i))
		return meta->error = -ERR_INVALID_INPUT;

	index = min(5, max(1, i));

	if (EFFECT_ID_CYCLE == meta->effect_id)
		pData = "\x96\x8C\x80\x6E\x68";
	else if (EFFECT_ID_BREATH == meta->effect_id)
		pData = "\x3C\x37\x31\x2C\x26";
	else if (EFFECT_ID_RAINBOW == meta->effect_id)
		pData = "\x72\x68\x64\x62\x61";
	else
		pData = "\x77\x74\x6E\x6B\x67";

	if (0 > (meta->error = select_packet_cache(meta->plasma, meta->effect_index)))
		return meta->error;

	if (0 > (meta->error = set_packet_byte(meta->plasma, OFF_PKT_EFFECT_P1, pData[index - 1])))
		return meta->error;

	meta->flush_profile++;

	return count;
}

static ssize_t get_setting_direction(struct attribute_meta *meta, char *buf)
{
	byte value;

	if (EFFECT_ID_SWIRL != meta->effect_id && EFFECT_ID_CHASE != meta->effect_id) {
		meta->error = -ERR_ATTR_NOT_SUPPORTED;
		return sprintf(buf, "null");
	}

	if (!meta->zone_enabled) {
		meta->error = -ERR_ATTR_NOT_AVAILABLE;
		return sprintf(buf, "null");
	}

	if (0 > (meta->error = select_packet_cache(meta->plasma, meta->effect_index)))
		return sprintf(buf, "error");

	// If 0x01 bit is set on p2 the direction is counter clockwise
	value = get_packet_byte(meta->plasma, OFF_PKT_EFFECT_P2);

	if ((value & 0x01) == 0)
		return sprintf(buf, "CW");

	return sprintf(buf, "CCW");
}

static ssize_t put_setting_direction(struct attribute_meta *meta, const char *buf, size_t count)
{
	byte value;
	int index = read_string(buf, "CW") ? 0x00 : 0x01;

	if (EFFECT_ID_SWIRL != meta->effect_id && EFFECT_ID_CHASE != meta->effect_id)
		return meta->error = -ERR_ATTR_NOT_SUPPORTED;

	if (!meta->zone_enabled)
		return meta->error = -ERR_ATTR_NOT_AVAILABLE;

	if (0 > (meta->error = select_packet_cache(meta->plasma, meta->effect_index)))
		return meta->error;

	value = get_packet_byte(meta->plasma, OFF_PKT_EFFECT_P2) & ~0x01;

	if (0 > (meta->error = set_packet_byte(meta->plasma, OFF_PKT_EFFECT_P2, value | index)))
		return meta->error;

	meta->flush_profile++;

	return count;
}

static ssize_t get_setting_repeat(struct attribute_meta *meta, char *buf)
{
	byte value;

	if (EFFECT_ID_MORSE != meta->effect_id) {
		meta->error = -ERR_ATTR_NOT_SUPPORTED;
		return sprintf(buf, "null");
	}

	if (!meta->zone_enabled) {
		meta->error = -ERR_ATTR_NOT_AVAILABLE;
		return sprintf(buf, "null");
	}

	if (0 > (meta->error = select_packet_cache(meta->plasma, meta->effect_index)))
		return sprintf(buf, "error");

	// p4 is either 0x01 for once, or 0xFF for repeat
	value = get_packet_byte(meta->plasma, OFF_PKT_EFFECT_P4);

	if (0x01 == value)
		return sprintf(buf, "0");

	return sprintf(buf, "1");

}

static ssize_t put_setting_repeat(struct attribute_meta *meta, const char *buf, size_t count)
{
	int i;
	byte value;

	if (EFFECT_ID_MORSE != meta->effect_id)
		return meta->error = -ERR_ATTR_NOT_SUPPORTED;

	if (!meta->zone_enabled)
		return meta->error = -ERR_ATTR_NOT_AVAILABLE;

	if (1 != sscanf(buf, "%1d", &i))
		return meta->error = -ERR_INVALID_INPUT;

	// Anything other than 0x01 will cause a repeat
	// WHY does this need a run-once??
	value = i == 1 ? 0xFF : 0x01;

	if (0 > (meta->error = select_packet_cache(meta->plasma, meta->effect_index)))
		return meta->error;

	if (0 > (meta->error = set_packet_byte(meta->plasma, OFF_PKT_EFFECT_P4, value)))
		return meta->error;

	meta->flush_profile++;

	return count;
}

static ssize_t get_setting_mirage(struct attribute_meta *meta, char *buf)
{
	byte value;
	word r, g, b;

	// This setting has 2 pages: IDX_FREQUENCY which stores the values for
	// each of the 3 memory slots, and IDX_RESOLUTION which contains the
	// active timings for the applied slot.
	if (IDX_ZONE_FAN != meta->effect_index) {
		meta->error = -ERR_ATTR_NOT_SUPPORTED;
		return sprintf(buf, "null");
	}

	if (!meta->zone_enabled) {
		meta->error = -ERR_ATTR_NOT_AVAILABLE;
		return sprintf(buf, "null");
	}

	if (0 > (meta->error = select_packet_cache(meta->plasma, IDX_FREQUENCY)))
		return sprintf(buf, "error");

	dev_dbg(&meta->plasma->interface->dev, "slot: %d", meta->attr_slot);

	if (0 == meta->attr_slot) {
		value = get_packet_byte(meta->plasma, OFF_PKT_FREQUENCY_FLAGS);
		dev_dbg(&meta->plasma->interface->dev, "value: 0x%02X", value);

		if (0x03 == value) {
			value = 1;
		} else if (0x07 == value) {
			value = 2;
		} else if (0x0B == value) {
			value = 3;
		} else { // 0x0F indicates off
			// A null would indicate not available
			value = 0;
		}

		return sprintf(buf, "%d", value);
	}

	value = 2 * (meta->attr_slot - 1);

	r = get_packet_word(meta->plasma, OFF_PKT_FREQUENCY_R + value);
	g = get_packet_word(meta->plasma, OFF_PKT_FREQUENCY_G + value);
	b = get_packet_word(meta->plasma, OFF_PKT_FREQUENCY_B + value);

	// TODO update timings if any of these fail
	if (45 > r || r > 2000) r = 330;
	if (45 > g || g > 2000) g = 330;
	if (45 > b || b > 2000) b = 330;

	return sprintf(buf, "r=0x%04X g=0x%04X b=0x%04X", r, g, b);
}

static ssize_t put_setting_mirage(struct attribute_meta *meta, const char *buf, size_t count)
{
	int i, index, scanned, read, lo;
	byte active, hi;
	const char *pIter;
	word rgb[3] = {0};
	byte data[4][4] = {
		{0x01, 0x00, 0xFF, 0x4A},
		{0x02, 0x00, 0xFF, 0x4A},
		{0x03, 0x00, 0xFF, 0x4A},
		{0x04, 0x00, 0xFF, 0x4A}
	};
	const byte *pData = "\x00\x03\x07\x0B";
	const char *pNames = "rgbRGB";
	const char *pDelims = " \t\r\n,";

	if (IDX_ZONE_FAN != meta->effect_index)
		return meta->error = -ERR_ATTR_NOT_SUPPORTED;

	if (!meta->zone_enabled)
		return meta->error = -ERR_ATTR_NOT_AVAILABLE;

	if (0 > (meta->error = select_packet_cache(meta->plasma, IDX_FREQUENCY)))
		return meta->error;

	if (0 == meta->attr_slot) {
		if (1 != (read = read_number(buf, &scanned)))
			return meta->error = -ERR_INVALID_INPUT;

		index = max(0, min(3, scanned));
		active = pData[index]; // apply this value to IDX_FREQUENCY and updae timings in IDX_RESOLUTION

		if (0 != index) {
			index *= 2;
			// Read the RGB values from IDX_FREQUENCY
			rgb[0] = get_packet_word(meta->plasma, OFF_PKT_FREQUENCY_R + index);
			rgb[1] = get_packet_word(meta->plasma, OFF_PKT_FREQUENCY_G + index);
			rgb[2] = get_packet_word(meta->plasma, OFF_PKT_FREQUENCY_B + index);
		}
	} else {
		// Discover which slot is currently active
		active = get_packet_byte(meta->plasma, OFF_PKT_FREQUENCY_FLAGS);

		// Read the RGB values from input
		// Should accept three numbers ([rgb]=)?0xH{1,8}|[0-9]{}
		for (pIter = buf, i = 0; i < 3 && 0 != *pIter; ++i) {
			if (NULL != strchr(pNames, pIter[0]) && '=' == pIter[1])
				pIter += 2;
			if (0 >= (read = read_number(pIter, &scanned)) || 0xFFFF < scanned) {
				dev_dbg(&meta->plasma->interface->dev, "read %d, scanned: %d", read, scanned);
				break;
			}
			rgb[i] = max(45, min(2000, scanned & 0xFFFF));
			pIter += read;
			while (NULL != strchr(pDelims, pIter[0]))
				pIter++;
		}

		if (3 != i) {
			dev_dbg(&meta->plasma->interface->dev, "found: %d, read %d", i, pIter - buf);
			return meta->error = -ERR_INVALID_INPUT;
		}
	}

	if (0 != rgb[0]) {
		// Convert each color value to a frequency
		for (i = 0; i < 3; ++i) {
			hi = 0;
			lo = scanned = 48000000 / rgb[i];

			while (0xFFFF < lo) {
				hi++;
				lo = scanned / (hi + 1);
			}

			data[i + 1][1] = hi;
			// NOTE this is a little endian word
			data[i + 1][2] = lo & 0xFF;
			data[i + 1][3] = (lo >> 8) & 0xFF;
		}
	}

	if (0 > (meta->error = select_packet_cache(meta->plasma, IDX_RESOLUTION)))
		return meta->error;

	if (0 > (meta->error = reset_packet_header(meta->plasma, MODE_PROFILE|MODE_WRITE, PKT_RESOLUTION, 0x00, 0x00, 0x00)))
		return meta->error;

	if (0 > (meta->error = write_packet_bytes(meta->plasma, 4, &data[0][0], 16)))
		return meta->error;

	meta->flush_mirage++;
	meta->flush_profile++;

	return count;
}

static ssize_t get_setting_morse(struct attribute_meta *meta, char *buf)
{
	int i;
	byte *const *pMorseData;

	if (NULL == (pMorseData = get_morse_packets(meta)))
		return meta->error = -ERR_UNEXPECTED_VALUE;

	dev_dbg(&meta->plasma->interface->dev, "slot: %d", meta->attr_slot);

	if (0 == meta->attr_slot) {
		for (i = 2; i < 8; i += 2) {
			if (0 != memcmp(pMorseData[0], pMorseData[i], 60))
				continue;
			if (0 == memcmp(pMorseData[1], pMorseData[i + 1], 60))
				return sprintf(buf, "%d", i / 2);
		}

		// TODO - Reset the morse if error
		return sprintf(buf, "error");
	}

	return morse_to_ascii(meta->plasma, meta->attr_slot, buf);
}

static ssize_t put_setting_morse(struct attribute_meta *meta, const char *buf, size_t count)
{
	int read, scanned, index;
	byte *const *pMorseData;

	if (!meta->zone_enabled)
		return meta->error = -ERR_ATTR_NOT_AVAILABLE;

	if (NULL == (pMorseData = get_morse_packets(meta)))
		return meta->error = -ERR_UNEXPECTED_VALUE;

	if (0 == meta->attr_slot) {
		if (1 != (read = read_number(buf, &scanned)))
			return meta->error = -ERR_INVALID_INPUT;

		index = max(1, min(3, scanned));
		index *= 2;
	} else {
		index = meta->attr_slot * 2;

		if (0 > (meta->error = ascii_to_morse(meta->plasma, &pMorseData[index], buf, count)))
			return meta->error;
	}

	memcpy(pMorseData[0], pMorseData[index], 60);
	memcpy(pMorseData[1], pMorseData[index + 1], 60);

	// Sending the morse packets alone will not make it display.
	// The whole profile needs sending with the 2 morse pages embedded.
	meta->flush_profile++;
	meta->flush_morse++;

	return count;
}

static ssize_t variable_flush(struct attribute_meta *meta, size_t count)
{
	if (meta->flush_profile) {
		if (0 > (meta->error = flush_effect_packets(meta))) {
			dev_dbg(&meta->plasma->interface->dev, "flush profile failed: (%d)", meta->error);
			return meta->error;
		}
	}

	// if (meta->flush_morse) {
	// 	if (0 > (meta->error = flush_morse_packets(meta))) {
	// 		dev_dbg(&meta->plasma->interface->dev, "flush morse failed: (%d)", meta->error);
	// 		return meta->error;
	// 	}
	// }

	return count;
}

static ssize_t variable_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t written;

	struct attribute_meta meta = get_attribute_meta(dev, attr);

	if (meta.error) {
		dev_dbg(dev, "variable_read: An error (%d) occured while parsing attribute meta.", meta.error);
		return 0;
	}

	dev_dbg(dev, "reading attribute: %s(%d)/%s(%d)", meta.zone_name, meta.effect_index, meta.attr_name, meta.attr_id);

	switch (meta.attr_id) {
		case ATTR_ENABLED:
			written = get_setting_enabled(&meta, buf);
			break;

		case ATTR_EFFECT:
			written = get_setting_effect(&meta, buf);
			break;

		case ATTR_COLOR:
			written = get_setting_color(&meta, buf);
			break;

		case ATTR_RANDOM:
			written = get_setting_random(&meta, buf);
			break;

		case ATTR_BRIGHTNESS:
			written = get_setting_brightness(&meta, buf);
			break;

		case ATTR_SPEED:
			written = get_setting_speed(&meta, buf);
			break;

		case ATTR_DIRECTION:
			written = get_setting_direction(&meta, buf);
			break;

		case ATTR_REPEAT:
			written = get_setting_repeat(&meta, buf);
			break;

		case ATTR_MIRAGE:
			written = get_setting_mirage(&meta, buf);
			break;

		case ATTR_MORSE:
			written = get_setting_morse(&meta, buf);
			break;

		default:
			meta.error = -ENOENT;
			written = 0;
			break;
	}

	if (0 > meta.error) {
		dev_dbg(dev, "read attribute failed: (%d)", meta.error);
		return written;
	}

	return variable_flush(&meta, written);
}

static ssize_t variable_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	ssize_t read;

	struct attribute_meta meta = get_attribute_meta(dev, attr);

	if (meta.error) {
		dev_dbg(dev, "variable_write: An error (%d) occured while parsing attribute meta.", meta.error);
		return meta.error;
	}

	dev_dbg(dev, "writing attribute: %s(%d)/%s(%d)", meta.zone_name, meta.effect_index, meta.attr_name, meta.attr_id);

	switch (meta.attr_id) {
		case ATTR_ENABLED:
			read = put_setting_enabled(&meta, buf, count);
			break;

		case ATTR_EFFECT:
			read = put_setting_effect(&meta, buf, count);
			break;

		case ATTR_COLOR:
			read = put_setting_color(&meta, buf, count);
			break;

		case ATTR_RANDOM:
			read = put_setting_random(&meta, buf, count);
			break;

		case ATTR_BRIGHTNESS:
			read = put_setting_brightness(&meta, buf, count);
			break;

		case ATTR_SPEED:
			read = put_setting_speed(&meta, buf, count);
			break;

		case ATTR_DIRECTION:
			read = put_setting_direction(&meta, buf, count);
			break;

		case ATTR_REPEAT:
			read = put_setting_repeat(&meta, buf, count);
			break;

		case ATTR_MIRAGE:
			read = put_setting_mirage(&meta, buf, count);
			break;

		case ATTR_MORSE:
			read = put_setting_morse(&meta, buf, count);
			break;

		default:
			meta.error = -ENOENT;
			read = 0;
			break;
	}

	if (0 > meta.error) {
		dev_dbg(dev, "write attribute failed: (%d)",meta.error);
		return meta.error;
	}

	return variable_flush(&meta, read);
}
