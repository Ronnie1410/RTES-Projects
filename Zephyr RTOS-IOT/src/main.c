/* 
 * Author:- Roshan Raj Kanakaiprath
 *
 * ASU ID:- 1222478062 
 * 
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(net_coap_server_sample, LOG_LEVEL_DBG);

#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <drivers/sensor.h>
#include <fsl_iomuxc.h>
#include <errno.h>
#include <sys/printk.h>
#include <sys/byteorder.h>
#include <zephyr.h>
#include <string.h>

#include <net/socket.h>
#include <net/net_mgmt.h>
#include <net/net_ip.h>
#include <net/udp.h>
#include <net/coap.h>
#include <net/coap_link_format.h>

#include "net_private.h"

// use DT marcos to get pin information for leds

#define LED_RED DT_NODELABEL(r_led)

#define LED0	DT_GPIO_LABEL(LED_RED, gpios)
#define PIN0	DT_GPIO_PIN(LED_RED, gpios)
#define FLAGS0	DT_GPIO_FLAGS(LED_RED, gpios)

#define LED_GREEN DT_NODELABEL(g_led)

#define LED1	DT_GPIO_LABEL(LED_GREEN, gpios)
#define PIN1	DT_GPIO_PIN(LED_GREEN, gpios)
#define FLAGS1	DT_GPIO_FLAGS(LED_GREEN, gpios)

#define LED_BLUE DT_NODELABEL(b_led)

#define LED2	DT_GPIO_LABEL(LED_BLUE, gpios)
#define PIN2	DT_GPIO_PIN(LED_BLUE, gpios)
#define FLAGS2	DT_GPIO_FLAGS(LED_BLUE, gpios)

#define MAX_COAP_MSG_LEN 256

#define MY_COAP_PORT 5683

#define BLOCK_WISE_TRANSFER_SIZE_GET 2048

#define NUM_OBSERVERS 2

#define NUM_PENDINGS 3

/* CoAP socket fd */
static int sock;

static struct coap_observer observers[NUM_OBSERVERS];

static struct coap_pending pendings[NUM_PENDINGS];

static struct k_work_delayable observer_work;

static struct coap_resource *resource_to_notify0, *resource_to_notify1;

static struct k_work_delayable retransmit_work;

/* For Device Binding*/

const struct device *gpiob,*gpiob3, *sensor_dev0, *sensor_dev1;

static int ledr_val=0, ledg_val=0, ledb_val=0, led_status, measure_counter = 0;
static struct sensor_value distance0, distance1;
double prev_dist0, prev_dist1;
bool distance_change = false;

K_THREAD_STACK_DEFINE(stack_area, 512);
static struct k_thread thread_data;
uint32_t sampling_period=1000;

//Define Timer
K_TIMER_DEFINE(sync_timer, NULL, NULL);

//Routine to get measurements from distance sensor
static int measure(const struct device *dev0, const struct device *dev1)
{
    int ret;
	double dist0, dist1;
	char meas0[20], meas1[20];

    ret = sensor_sample_fetch_chan(dev0, SENSOR_CHAN_ALL);
	ret = sensor_sample_fetch_chan(dev1, SENSOR_CHAN_ALL);
    switch (ret) {
    case 0:
        ret = sensor_channel_get(dev0, SENSOR_CHAN_DISTANCE, &distance0);
		ret = sensor_channel_get(dev1, SENSOR_CHAN_DISTANCE, &distance1);
        if (ret) {
            LOG_ERR("sensor_channel_get failed ret %d", ret);
            return ret;
        }
        LOG_INF("%s: %d.%02dinches", dev0->name, distance0.val1, (distance0.val2 / 10000));
		LOG_INF("%s: %d.%02dinches", dev1->name, distance1.val1, (distance1.val2 / 10000));
		snprintk(meas0, sizeof(meas0),"%d.%02d", distance0.val1,(distance0.val2 / 10000));
		snprintk(meas1, sizeof(meas1),"%d.%02d", distance1.val1,(distance1.val2 / 10000));
		dist0 = atof(meas0);
		dist1 = atof(meas1);
		if(measure_counter)
		{
			if(abs(dist0-prev_dist0)>0.5f || abs(dist1-prev_dist1)>0.5)
			{
				distance_change = true;
				//LOG_INF("TRUE");
			}
		}
		prev_dist0 = dist0;
		prev_dist1 = dist1;
		measure_counter++;
        break;
    case -EIO:
        LOG_WRN("%s or %s: Could not read devices", dev0->name, dev1->name);
        break;
    default:
        LOG_ERR("Error when reading devices: %s or %s", dev0->name, dev1->name);
        break;
    }
    return 0;
}

void sensor_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	int ret;
	k_timer_start(&sync_timer, K_MSEC(sampling_period), K_MSEC(sampling_period));
    while (1) {
        k_timer_status_sync(&sync_timer);
        ret = measure(sensor_dev0, sensor_dev1);
        if (ret) {
            return;
        }
    }
    LOG_INF("exiting");
}

static int start_coap_server(void)
{
	struct sockaddr_in addr;
	int r;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(MY_COAP_PORT);

	sock = socket(addr.sin_family, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("Failed to create UDP socket %d", errno);
		return -errno;
	}

	r = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (r < 0) {
		LOG_ERR("Failed to bind UDP socket %d", errno);
		return -errno;
	}

	return 0;
}

static int send_coap_reply(struct coap_packet *cpkt,
			   const struct sockaddr *addr,
			   socklen_t addr_len)
{
	int r;

	net_hexdump("Response", cpkt->data, cpkt->offset);

	r = sendto(sock, cpkt->data, cpkt->offset, 0, addr, addr_len);
	if (r < 0) {
		LOG_ERR("Failed to send %d", errno);
		r = -errno;
	}

	return r;
}

static int well_known_core_get(struct coap_resource *resource,
			       struct coap_packet *request,
			       struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_packet response;
	uint8_t *data;
	int r;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_well_known_core_get(resource, request, &response,
				     data, MAX_COAP_MSG_LEN);
	if (r < 0) {
		goto end;
	}

	r = send_coap_reply(&response, addr, addr_len);

end:
	k_free(data);

	return r;
}

static int led_get(struct coap_resource *resource,
			 struct coap_packet *request,
			 struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_packet response;
	uint8_t payload[40];
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint8_t *data;
	uint16_t id;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	int r;
	int channel = (int)((struct coap_core_metadata*)resource->user_data)->user_data;
	if(channel == 0)
    {
        led_status = ledr_val;
    }
    if(channel == 1)
    {
        led_status = ledg_val;
    }
    if(channel == 2)
    {
        led_status = ledb_val;
    }

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_INF("*******");
	LOG_INF("type: %u code %u id %u", type, code, id);
	LOG_INF("*******");

	if (type == COAP_TYPE_CON) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_NON_CON;
	}

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, type, tkl, token,
			     COAP_RESPONSE_CODE_CONTENT, id);
	if (r < 0) {
		goto end;
	}

	r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
				   COAP_CONTENT_FORMAT_TEXT_PLAIN);
	if (r < 0) {
		goto end;
	}

	r = coap_packet_append_payload_marker(&response);
	if (r < 0) {
		goto end;
	}

	/* The response that coap-client expects */
	if(led_status)
	{
		r = snprintk((char *) payload, sizeof(payload),"ON");
	}
	else{
		r = snprintk((char *) payload, sizeof(payload), "OFF");		
	}
	if (r < 0) {
		goto end;
	}

	r = coap_packet_append_payload(&response, (uint8_t *)payload,
				       strlen(payload));
	if (r < 0) {
		goto end;
	}

	r = send_coap_reply(&response, addr, addr_len);

end:
	k_free(data);

	return r;
}

static int led_put(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_packet response;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	const uint8_t *payload;
	uint8_t *data;
	uint16_t payload_len;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	uint16_t id;
	int r;

    int channel = (int)((struct coap_core_metadata*)resource->user_data)->user_data;

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_INF("*******");
	LOG_INF("type: %u code %u id %u", type, code, id);
	LOG_INF("*******");

	payload = coap_packet_get_payload(request, &payload_len);
	if (payload) {
		net_hexdump("PUT Payload", payload, payload_len);
	}
    
    if(channel == 0)
    {
        ledr_val = atoi((char*)payload);
        gpio_pin_set(gpiob, PIN0, ledr_val);
    }
    
    if(channel == 1)
    {
        ledg_val = atoi((char*)payload);
        gpio_pin_set(gpiob, PIN1, ledg_val);
    }
    if(channel == 2)
    {
        ledb_val = atoi((char*)payload);
        gpio_pin_set(gpiob3, PIN2, ledb_val);
    }

	if (type == COAP_TYPE_CON) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_NON_CON;
	}

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, type, tkl, token,
			     COAP_RESPONSE_CODE_CHANGED, id);
	if (r < 0) {
		goto end;
	}

	r = send_coap_reply(&response, addr, addr_len);

end:
	k_free(data);

	return r;
}

static void update_distance(struct k_work *work)
{

	if(distance_change == true)
	{
		if(resource_to_notify0)
		{
			coap_resource_notify(resource_to_notify0);
		}
		if(resource_to_notify1)
		{
			coap_resource_notify(resource_to_notify1);
		}
	}
	distance_change = false;	
	k_work_reschedule(&observer_work, K_SECONDS(2));
}
static void retransmit_request(struct k_work *work)
{
	struct coap_pending *pending;

	pending = coap_pending_next_to_expire(pendings, NUM_PENDINGS);
	if (!pending) {
		return;
	}

	if (!coap_pending_cycle(pending)) {
		k_free(pending->data);
		coap_pending_clear(pending);
		return;
	}

	k_work_reschedule(&retransmit_work, K_MSEC(pending->timeout));
}

static int create_pending_request(struct coap_packet *response,
				  const struct sockaddr *addr)
{
	struct coap_pending *pending;
	int r;

	pending = coap_pending_next_unused(pendings, NUM_PENDINGS);
	if (!pending) {
		return -ENOMEM;
	}

	r = coap_pending_init(pending, response, addr,
			      COAP_DEFAULT_MAX_RETRANSMIT);
	if (r < 0) {
		return -EINVAL;
	}

	coap_pending_cycle(pending);

	pending = coap_pending_next_to_expire(pendings, NUM_PENDINGS);
	if (!pending) {
		return 0;
	}

	k_work_reschedule(&retransmit_work, K_MSEC(pending->timeout));

	return 0;
}

static int send_notification_packet(const struct sockaddr *addr,
				    socklen_t addr_len,
				    uint16_t age, uint16_t id,
				    const uint8_t *token, uint8_t tkl,
				    bool is_response, const struct sensor_value distance)
{
	struct coap_packet response;
	char payload[40];
	uint8_t *data;
	uint8_t type;
	int r;

	if (is_response) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_CON;
	}

	if (!is_response) {
		id = coap_next_id();
	}

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, type, tkl, token,
			     COAP_RESPONSE_CODE_CONTENT, id);
	if (r < 0) {
		goto end;
	}

	if (age >= 2U) {
		r = coap_append_option_int(&response, COAP_OPTION_OBSERVE, age);
		if (r < 0) {
			goto end;
		}
	}

	r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
				   COAP_CONTENT_FORMAT_TEXT_PLAIN);
	if (r < 0) {
		goto end;
	}

	r = coap_packet_append_payload_marker(&response);
	if (r < 0) {
		goto end;
	}

	/* The response that coap-client expects */
	r = snprintk((char *) payload, sizeof(payload),
		     "%d.%02d inches\n", distance.val1,(distance.val2 / 10000));
	if (r < 0) {
		goto end;
	}

	r = coap_packet_append_payload(&response, (uint8_t *)payload,
				       strlen(payload));
	if (r < 0) {
		goto end;
	}

	if (type == COAP_TYPE_CON) {
		r = create_pending_request(&response, addr);
		if (r < 0) {
			goto end;
		}
	}

	k_work_reschedule(&observer_work, K_SECONDS(2));

	r = send_coap_reply(&response, addr, addr_len);

	/* On succesfull creation of pending request, do not free memory */
	if (type == COAP_TYPE_CON) {
		return r;
	}

end:
	k_free(data);

	return r;
}

static int hcsr_get(struct coap_resource *resource,
		   struct coap_packet *request,
		   struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_observer *observer;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint16_t id;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	bool observe = true;
	struct sensor_value distance;
	int sensor = (int)((struct coap_core_metadata*)resource->user_data)->user_data;

	if(sensor == 0)
	{
		distance = distance0;
	}
	else{
		distance = distance1;
	}

	if (!coap_request_is_observe(request)) {
		observe = false;
		goto done;
	}

	observer = coap_observer_next_unused(observers, NUM_OBSERVERS);
	if (!observer) {
		return -ENOMEM;
	}

	coap_observer_init(observer, request, addr);

	coap_register_observer(resource, observer);

	if(sensor == 0){
		resource_to_notify0 = resource;}
	else{
		resource_to_notify1 = resource;}
done:
	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_INF("*******");
	LOG_INF("type: %u code %u id %u", type, code, id);
	LOG_INF("*******");

	return send_notification_packet(addr, addr_len,
					observe ? resource->age : 0,
					id, token, tkl, true,distance);
}

static void hcsr_notify(struct coap_resource *resource,
		       struct coap_observer *observer)
{	
	struct sensor_value distance;
	int sensor = (int)((struct coap_core_metadata*)resource->user_data)->user_data;
	if(sensor == 0)
	{
		distance = distance0;
	}
	else{
		distance = distance1;
	}

	send_notification_packet(&observer->addr,
				 sizeof(observer->addr),
				 resource->age, 0,
				 observer->token, observer->tkl, false, distance);
}


static int sensor_period_put(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_packet response;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	const uint8_t *payload;
	uint8_t *data;
	uint16_t payload_len;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	uint16_t id;
	int r;

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_INF("*******");
	LOG_INF("type: %u code %u id %u", type, code, id);
	LOG_INF("*******");

	payload = coap_packet_get_payload(request, &payload_len);
	if (payload) {
		net_hexdump("PUT Payload", payload, payload_len);
	}
    
    sampling_period = atoi((char*)payload);
	k_timer_start(&sync_timer, K_MSEC(sampling_period), K_MSEC(sampling_period));


	if (type == COAP_TYPE_CON) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_NON_CON;
	}

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, type, tkl, token,
			     COAP_RESPONSE_CODE_CHANGED, id);
	if (r < 0) {
		goto end;
	}

	r = send_coap_reply(&response, addr, addr_len);

end:
	k_free(data);

	return r;
}

static const char * const led_r_path[] = { "led", "led_r", NULL };
static const char * const led_g_path[] = { "led", "led_g", NULL };
static const char * const led_b_path[] = { "led", "led_b", NULL };
static const char * const hcsr_0_path[] = { "sensor", "hcsr_0", NULL };
static const char * const hcsr_1_path[] = { "sensor", "hcsr_1", NULL };
static const char * const hcsr_period_path[] = { "sensor", "period", NULL };

static struct coap_resource resources[] = {
	{ .get = well_known_core_get,
	  .path = COAP_WELL_KNOWN_CORE_PATH,
	},
	{ .get = led_get,
	  .put = led_put,
	  .path = led_r_path,
      .user_data = &((struct coap_core_metadata){
            .attributes = NULL,
            .user_data = (void*)0,
        }),
	},
	{ .get = led_get,
	  .put = led_put,
	  .path = led_g_path,
      .user_data = &((struct coap_core_metadata){
            .attributes = NULL,
            .user_data = (void*)1,
        }),
	},
	{ .get = led_get,
	  .put = led_put,
	  .path = led_b_path,
      .user_data = &((struct coap_core_metadata){
            .attributes = NULL,
            .user_data = (void*)2,
        }),
	},
	{ .get = hcsr_get,
	  .path = hcsr_0_path,
      .user_data = &((struct coap_core_metadata){
            .attributes = NULL,
            .user_data = (void*)0,
        }),
	  .notify = hcsr_notify,
	},
	{ .get = hcsr_get,
	  .path = hcsr_1_path,
      .user_data = &((struct coap_core_metadata){
            .attributes = NULL,
            .user_data = (void*)1,
        }),
	  .notify = hcsr_notify,
	},
	{ .put = sensor_period_put,
	  .path = hcsr_period_path,
	},	
    { },
};

//Routine to find resources by observer
static struct coap_resource *find_resouce_by_observer(
		struct coap_resource *resources, struct coap_observer *o)
{
	struct coap_resource *r;

	for (r = resources; r && r->path; r++) {
		sys_snode_t *node;

		SYS_SLIST_FOR_EACH_NODE(&r->observers, node) {
			if (&o->list == node) {
				return r;
			}
		}
	}

	return NULL;
}

//Routine to process request
static void process_coap_request(uint8_t *data, uint16_t data_len,
				 struct sockaddr *client_addr,
				 socklen_t client_addr_len)
{
	struct coap_packet request;
	struct coap_pending *pending;
	struct coap_option options[16] = { 0 };
	uint8_t opt_num = 16U;
	uint8_t type;
	int r;

	r = coap_packet_parse(&request, data, data_len, options, opt_num);
	if (r < 0) {
		LOG_ERR("Invalid data received (%d)\n", r);
		return;
	}

	type = coap_header_get_type(&request);

	pending = coap_pending_received(&request, pendings, NUM_PENDINGS);
	if (!pending) {
		goto not_found;
	}

	/* Clear CoAP pending request */
	if (type == COAP_TYPE_ACK) {
		k_free(pending->data);
		coap_pending_clear(pending);
	}

	return;

not_found:

	if (type == COAP_TYPE_RESET) {
		struct coap_resource *r;
		struct coap_observer *o;

		o = coap_find_observer_by_addr(observers, NUM_OBSERVERS,
					       client_addr);
		if (!o) {
			LOG_ERR("Observer not found\n");
			goto end;
		}

		r = find_resouce_by_observer(resources, o);
		if (!r) {
			LOG_ERR("Observer found but Resource not found\n");
			goto end;
		}

		coap_remove_observer(r, o);

		return;
	}

end:
	r = coap_handle_request(&request, resources, options, opt_num,
				client_addr, client_addr_len);
	if (r < 0) {
		LOG_WRN("No handler for such request (%d)\n", r);
	}
}

//Routine to process client request
static int process_client_request(void)
{
	int received;
	struct sockaddr client_addr;
	socklen_t client_addr_len;
	uint8_t request[MAX_COAP_MSG_LEN];

	do {
		client_addr_len = sizeof(client_addr);
		received = recvfrom(sock, request, sizeof(request), 0,
				    &client_addr, &client_addr_len);
		if (received < 0) {
			LOG_ERR("Connection error %d", errno);
			return -errno;
		}

		process_coap_request(request, received, &client_addr,
				     client_addr_len);
	} while (true);

	return 0;
}

//Main Rotine
void main(void)
{
	int r, ret;

	LOG_DBG("Start CoAP-server");

	k_tid_t tid = k_thread_create(&thread_data, stack_area,
                                 K_THREAD_STACK_SIZEOF(stack_area),
                                 sensor_thread,
                                 NULL, NULL, NULL,
                                 0, 0, K_FOREVER);
	// hard-coded statements to set up IOMUX for led and interrupt pins
	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_10_GPIO1_IO10, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_10_GPIO1_IO10,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_11_GPIO1_IO11, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_11_GPIO1_IO11,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_03_GPIO3_IO15, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_03_GPIO3_IO15,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B1_08_GPIO1_IO24, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B1_08_GPIO1_IO24,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B1_02_GPIO1_IO18, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B1_02_GPIO1_IO18,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_03_GPIO1_IO03, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_03_GPIO1_IO03,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_02_GPIO1_IO02, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_02_GPIO1_IO02,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));
	
	//Get Device bindings
	sensor_dev0 = device_get_binding("HC-SR04_0");
	if (!sensor_dev0) {
		printk("error binding sensor 0\n");
		return;
	}
	//LOG_INF("dev is %p, name is %s", dev, dev->name);
	sensor_dev1 = device_get_binding("HC-SR04_1");
	if (!sensor_dev1) {
		printk("error binding sensor 1\n");
		return;
	}

    gpiob=device_get_binding(LED0);
	if (!gpiob) {
		printk("error binding gpio1\n");
		return;
	}
	gpiob3=device_get_binding(LED2);
	if (!gpiob3) {
		printk("error binding gpio3\n");
		return;
	} 

	//Configure GPIOs
    ret=gpio_pin_configure(gpiob, PIN0, GPIO_OUTPUT_ACTIVE | FLAGS0); 
	if(ret<0)
		printk("error configuring gpio pin 11 \n");

	ret=gpio_pin_configure(gpiob, PIN1, GPIO_OUTPUT_ACTIVE | FLAGS1); 
	if(ret<0)
		printk("error configuring gpio pin 10 \n");

	ret=gpio_pin_configure(gpiob3, PIN2, GPIO_OUTPUT_ACTIVE | FLAGS2); 
	if(ret<0)
		printk("error configuring gpio pin 15 \n");
	//Set all channels of Led to OFF
    gpio_pin_set(gpiob, PIN0, ledr_val);
    gpio_pin_set(gpiob, PIN1, ledg_val);
    gpio_pin_set(gpiob3, PIN2, ledb_val);
	//Set previous measurements of distance sensors to zero
	prev_dist0 = 0.0f;
	prev_dist1 = 0.0f;

	//Start the Coap Server
	r = start_coap_server();
	if (r < 0) {
		goto quit;
	}

	//Start thread for distance measurement
	k_thread_start(tid);

	k_work_init_delayable(&retransmit_work, retransmit_request);
	k_work_init_delayable(&observer_work, update_distance);

	while (1) {
		r = process_client_request();
		if (r < 0) {
			goto quit;
		}
	}

	LOG_DBG("Done");
	return;

quit:
	LOG_ERR("Quit");
}
