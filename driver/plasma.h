#include <linux/usb.h>

typedef unsigned char byte;
typedef unsigned short word;

#define _dump_packet_selection(m, p) print_hex_dump_bytes( \
	m, \
	DUMP_PREFIX_NONE, \
	p->packet_selection, \
	p->packet_size )
#define _dump_packet_index(m, p, i) print_hex_dump_bytes( \
	m, \
	DUMP_PREFIX_NONE, \
	&p->packet_cache[p->packet_size * i], \
	p->packet_size )
#define _dump_packet_buffer(m, p) print_hex_dump_bytes( \
	m, \
	DUMP_PREFIX_NONE, \
	p->packet_buffer, \
	p->packet_size )

#define dump_bytes(m, p, s) print_hex_dump_bytes( \
	m, \
	DUMP_PREFIX_NONE, \
	p, \
	s )

#ifdef DEBUG
	#define dump_packet_selection _dump_packet_selection
	#define dump_packet_index _dump_packet_index
	#define dump_packet_buffer _dump_packet_buffer
#else
	#define dump_packet_selection(m, p) do { if(0) _dump_packet_selection(m, p); } while(0)
	#define dump_packet_index(m, p, i) do { if(0) _dump_packet_index(m, p, i); } while(0)
	#define dump_packet_buffer(m, p) do { if(0) _dump_packet_buffer(m, p); } while(0)
#endif

#define ERR_INVALID_INPUT			256
#define ERR_ATTR_NOT_AVAILABLE		257
#define ERR_ATTR_NOT_SUPPORTED		258
#define ERR_EFFECT_NOT_SUPPORTED	259
#define ERR_UNEXPECTED_VALUE		260

const int __foo = 1;
#define NEEDS_SWAP() ((*(char*)&__foo) == 0)
#define WORD_SWAP(n) ((unsigned short) (((n & 0xFF) << 8) | \
										((n & 0xFF00) >> 8)))
#define DWORD_SWAP(n) ((unsigned long) (((n & 0xFF) << 24) | \
										((n & 0xFF00) << 8) | \
										((n & 0xFF0000) >> 8) | \
										((n & 0xFF000000) >> 24)))

#define BASE_OF(n) ((n >= '0' && n <= '9') ? '0' : \
					(n >= 'a' && n <= 'f') ? 'a' - 10 : \
					(n >= 'A' && n <= 'F') ? 'A' - 10 : \
					'\255')
#define IS_HEX(n) (BASE_OF(n) != '\255')
#define TO_HEX(n) (n - BASE_OF(n))

/* Define these values to match your devices */
#define USB_PLASMA_VENDOR_ID	0x2516
#define USB_PLASMA_PRODUCT_ID	0x0051

/* table of devices that work with this driver */
static const struct usb_device_id plasma_table[] = {
	{ USB_DEVICE(USB_PLASMA_VENDOR_ID, USB_PLASMA_PRODUCT_ID) },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, plasma_table);


static ssize_t variable_read(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t variable_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

/* Get a minor range for your devices from the usb maintainer */
#define USB_PLASMA_MINOR_BASE	192

/* our private defines. if this grows any larger, use your own .h file */
#define MAX_TRANSFER			(PAGE_SIZE - 512)
/* MAX_TRANSFER is chosen so that the VM is not stressed by
   allocations > PAGE_SIZE and the number of packets in a page
   is an integer 512 is the largest possible packet on EHCI */
#define WRITES_IN_FLIGHT	8
/* arbitrarily chosen */

#define LOGO_ATTRIBUTE(value) \
	static struct device_attribute logo_##value##_attribute = __ATTR(value, (S_IWUSR | S_IRUGO), variable_read, variable_write); \

#define LOGO_ATTRIBUTE_ADDR(value) \
	&logo_##value##_attribute.attr

#define FAN_ATTRIBUTE(value) \
	static struct device_attribute fan_##value##_attribute = __ATTR(value, (S_IWUSR | S_IRUGO), variable_read, variable_write); \

#define FAN_ATTRIBUTE_ADDR(value) \
	&fan_##value##_attribute.attr

#define RING_ATTRIBUTE(value) \
	static struct device_attribute ring_##value##_attribute = __ATTR(value, (S_IWUSR | S_IRUGO), variable_read, variable_write); \

#define RING_ATTRIBUTE_ADDR(value) \
	&ring_##value##_attribute.attr

typedef struct attribute_meta {
	struct usb_plasma 	*plasma;
	const char 			*zone_name;
	const char 			*attr_name;
	bool 				zone_enabled;
	int 				error;
	int 				attr_id;
	int 				attr_slot;
	int 				effect_id;
	int 				effect_index;
	bool 				is_ring;
	int					flush_profile;
	int					flush_morse;
	int					flush_mirage;
} attribute_meta;

#define ATTR_EFFECT		1
#define ATTR_COLOR		2
#define ATTR_BRIGHTNESS	3
#define ATTR_SPEED		4
#define ATTR_DIRECTION	5
#define ATTR_REPEAT		6
#define ATTR_MIRAGE		7
#define ATTR_MORSE		8
#define ATTR_ENABLED	9
#define ATTR_RANDOM		10

#define IDX_PACKET_BUFFER	-1
#define IDX_EFFECT_STATIC	0
#define IDX_EFFECT_RAINBOW	1
#define IDX_EFFECT_SWIRL	2
#define IDX_EFFECT_CHASE	3
#define IDX_EFFECT_BOUNCE	4
#define IDX_EFFECT_MORSE	5
#define IDX_EFFECT_CYCLE	6
#define IDX_EFFECT_BREATH	7

#define EFFECT_NAME_STATIC	"static"
#define EFFECT_NAME_RAINBOW	"rainbow"
#define EFFECT_NAME_SWIRL	"swirl"
#define EFFECT_NAME_CHASE	"chase"
#define EFFECT_NAME_BOUNCE	"bounce"
#define EFFECT_NAME_MORSE	"morse"
#define EFFECT_NAME_CYCLE	"cycle"
#define EFFECT_NAME_BREATH	"breathing"

#define EFFECT_ID_STATIC	0x00
#define EFFECT_ID_RAINBOW	0x07
#define EFFECT_ID_SWIRL		0x0A
#define EFFECT_ID_CHASE		0x09
#define EFFECT_ID_BOUNCE	0x08
#define EFFECT_ID_MORSE		0x0B
#define EFFECT_ID_CYCLE		0x02
#define EFFECT_ID_BREATH	0x01

const char EffectMap[8] = {
	[IDX_EFFECT_STATIC]		= EFFECT_ID_STATIC,
	[IDX_EFFECT_RAINBOW]	= EFFECT_ID_RAINBOW,
	[IDX_EFFECT_SWIRL]		= EFFECT_ID_SWIRL,
	[IDX_EFFECT_CHASE]		= EFFECT_ID_CHASE,
	[IDX_EFFECT_BOUNCE]		= EFFECT_ID_BOUNCE,
	[IDX_EFFECT_MORSE]		= EFFECT_ID_MORSE,
	[IDX_EFFECT_CYCLE]		= EFFECT_ID_CYCLE,
	[IDX_EFFECT_BREATH]		= EFFECT_ID_BREATH,
};

// #define IDX_ZONE_FIRST		8
#define IDX_ZONE_LOGO 		8
#define IDX_ZONE_FAN 		9
// #define IS_IDX_RING(n) (n != IDX_ZONE_FAN && n != IDX_ZONE_LOGO)
// #define IDX_ZONE_RING 		10
#define ZONE_LENGTH			3
#define IDX_ACTIVE_ZONES	10

#define ZONE_ID_FAN			0x06
#define ZONE_ID_LOGO		0x05
// NOTE The ring does not have an ID

#define IDX_MORSE_FIRST		11
#define MORSE_LENGTH		8
#define IDX_MORSE_LAST		18

#define IDX_FREQUENCY		19
#define IDX_RESOLUTION		20 // This packet cannot be read, it must be built

// #define IDX_BREATH_FIRST	20
// #define BREATH_LENGTH		8
// #define IDX_BREATH_LAST		27

#define TOTAL_PACKETS		21


// A combination of the following flags are written
// to the first byte of a packet.
#define MODE_WRITE		0x01
#define MODE_READ		0x02
#define MODE_FIRMWARE	0x10
#define MODE_CONTROL	0x40
#define MODE_PROFILE	0x50

#define PKT_EFFECT			0x2C
#define PKT_RESOLUTION		0x71 // Cannot be read, 4 indexed 3 BYTE values (index 0 == off, 123 == RGB)
#define PKT_MORSE			0x73
#define PKT_FREQUENCY		0x94 // A triplet of 3 WORDs each (total 9 WORDs)
#define PKT_ACTIVE_ZONES	0xA0

// Byte offsets common to all packets
#define OFF_HEADER_MODE		0
#define OFF_HEADER_TYPE 	1
#define OFF_HEADER_COUNT	2 // sometimes this is just 1, others used as a zero based index
#define OFF_HEADER_FLAGS	3 // Acts like OFF_HEADER_COUNT

// Byte offsets into individual packets
#define OFF_PKT_ACTIVE_ZONES_LENGTH		5  // This isn't a 'length', it's value is always 3, maybe it's an offset?
#define OFF_PKT_ACTIVE_ZONES_ZONES		8
#define OFF_PKT_ACTIVE_ZONES_RING_ZONE	10

#define OFF_PKT_EFFECT_ID	4
#define OFF_PKT_EFFECT_P1	5
#define OFF_PKT_EFFECT_P2	6
#define OFF_PKT_EFFECT_P3	7
#define OFF_PKT_EFFECT_P4	8
#define OFF_PKT_EFFECT_P5	9
#define OFF_PKT_EFFECT_R	10
#define OFF_PKT_EFFECT_G	11
#define OFF_PKT_EFFECT_B	12

#define OFF_PKT_FREQUENCY_FLAGS	4  // 1 byte
#define OFF_PKT_FREQUENCY_R		5  // Each 1 word (+ n * 2 where n == slot)
#define OFF_PKT_FREQUENCY_G		11
#define OFF_PKT_FREQUENCY_B		17
