#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"

#include <string.h>
#include <stdio.h> /* For printf() */

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "Sink"
#define LOG_LEVEL LOG_LEVEL_INFO

/* Configuration */
#define BROADCAST_INTERVAL (600 * CLOCK_SECOND)

typedef struct {
    int level_num;
    int round_num;
} tree_formation_packet_t;

static tree_formation_packet_t tf_packet;
/*---------------------------------------------------------------------------*/
PROCESS(sink_process, "Sink process");
AUTOSTART_PROCESSES(&sink_process);

/*---------------------------------------------------------------------------*/
void input_callback(const void *data, uint16_t len,
  const linkaddr_t *src, const linkaddr_t *dest)
{
    // if(strncmp(((char *) data), "M4", 2) == 0) {
    //     LOG_INFO("Received '%s' from %02x:%02x\n", (char *) data, src->u8[0], src->u8[1]);
    // } 
}   
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(sink_process, ev, data)
{
    static struct etimer periodic_timer;

    PROCESS_BEGIN();

    // Initialize NullNet
    nullnet_set_input_callback(input_callback);

    // Set timer for broadcast
    etimer_set(&periodic_timer, BROADCAST_INTERVAL);
    
    while(1) {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));

        // Initialize the tf packet
        tf_packet.level_num = 0;
        tf_packet.round_num++;

        nullnet_buf = (uint8_t *)&tf_packet;
        nullnet_len = sizeof(tf_packet);

        LOG_INFO("Broadcast TF packet with level number: %d and round number: %d\n", tf_packet.level_num, tf_packet.round_num);

        NETSTACK_NETWORK.output(NULL);
        
        // Reset timer
        etimer_reset(&periodic_timer);
    }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/