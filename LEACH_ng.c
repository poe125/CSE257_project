#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"

#include <string.h>
#include <stdio.h> /* For printf() */

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "LEACH_Node"
#define LOG_LEVEL LOG_LEVEL_INFO

/* Configuration */
#define ADV_INTERVAL (600 * CLOCK_SECOND)
#define ROUND_INTERVAL (6000 * CLOCK_SECOND)
#define DATA_INTERVAL (300 * CLOCK_SECOND)

typedef struct {
    uint8_t node_id;
    uint8_t cluster_head;
    uint8_t data_value;
} leach_packet_t;

static leach_packet_t leach_packet;

/*---------------------------------------------------------------------------*/
PROCESS(leach_node_process, "LEACH Node Process");
AUTOSTART_PROCESSES(&leach_node_process);

/*---------------------------------------------------------------------------*/
void input_callback(const void *data, uint16_t len,
  const linkaddr_t *src, const linkaddr_t *dest)
{
    // Handle incoming data if needed
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(leach_node_process, ev, data)
{
    static struct etimer adv_timer;
    static struct etimer round_timer;
    static struct etimer data_timer;

    PROCESS_BEGIN();

    // Initialize NullNet
    nullnet_set_input_callback(input_callback);

    // Set timer for initial advertisement
    etimer_set(&adv_timer, ADV_INTERVAL);

    while(1) {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&adv_timer));

        // Node broadcasts an advertisement
        leach_packet.node_id = linkaddr_node_addr.u8[0];
        leach_packet.cluster_head = 0;  // Placeholder for cluster head information
        leach_packet.data_value = 0;    // Placeholder for data value

        nullnet_buf = (uint8_t *)&leach_packet;
        nullnet_len = sizeof(leach_packet);

        LOG_INFO("Node %u broadcasts an advertisement\n", leach_packet.node_id);

        NETSTACK_NETWORK.output(NULL);

        // Set timer for next round
        etimer_set(&round_timer, ROUND_INTERVAL);

        while (1) {
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&round_timer));

            // Node checks if it should be a cluster head for this round
            // Implement LEACH cluster head selection logic here
            if (should_be_cluster_head()) {
                leach_packet.cluster_head = 1;

                LOG_INFO("Node %u elected as a cluster head\n", leach_packet.node_id);
            } else {
                leach_packet.cluster_head = 0;
            }

            // Set timer for sending data if not a cluster head
            if (!leach_packet.cluster_head) {
                etimer_set(&data_timer, DATA_INTERVAL);
            }

            while (1) {
                PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&data_timer));

                // Node sends data to its cluster head
                if (!leach_packet.cluster_head) {
                    leach_packet.data_value = generate_data_value();  // Implement data generation logic
                    nullnet_buf = (uint8_t *)&leach_packet;
                    nullnet_len = sizeof(leach_packet);

                    LOG_INFO("Node %u sends data to cluster head %u: %u\n",
                             leach_packet.node_id, leach_packet.cluster_head, leach_packet.data_value);

                    NETSTACK_NETWORK.output(NULL);
                }

                // Reset data timer for the next data transmission
                etimer_reset(&data_timer);
            }

            // Reset round timer for the next round
            etimer_reset(&round_timer);
        }
    }

    PROCESS_END();
}
/*---------------------------------------------------------------------------*/