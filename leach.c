#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "net/packetbuf.h"
#include "LEACH.h"

#include <string.h>
#include <stdio.h> /* For printf() */
#include <stdbool.h>
#include <stdlib.h>
#include <random.h>
#include <etimer.h>
#include <process.h>

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "LEACH_Node"
#define LOG_LEVEL LOG_LEVEL_INFO

/* Configuration */
#define ADV_INTERVAL (600 * CLOCK_SECOND)
#define ROUND_INTERVAL (6000 * CLOCK_SECOND)
#define DATA_INTERVAL (300 * CLOCK_SECOND)
#define P 0.05

typedef struct {
    uint8_t node_id;
    uint8_t cluster_head;
    uint8_t data_value;
    int rssi;
    int energy_usage;
} leach_packet_t;

// Define the broadcast message payload structure
typedef struct {
    char data[12];
} broadcast_message_t;

static leach_packet_t leach_packet;
static volatile bool listen_enabled = true;

/*---------------------------------------------------------------------------*/
PROCESS(leach_node_process, "LEACH Node Process");
AUTOSTART_PROCESSES(&leach_node_process, &sink_process);

/*---------------------------------------------------------------------------*/

void input_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest){
    LOG_INFO("Inside input_callback\n");
    if (listen_enabled) {
        LOG_INFO("Listen is enabled\n");
        if (linkaddr_cmp(dest, &linkaddr_null)) {
            // Broadcast message
            broadcast_message_t *broadcast_msg = (broadcast_message_t *)data;
            if (strncmp(broadcast_msg->data, "hello", sizeof(broadcast_msg->data)) == 0) {
                // Use packetbuf_attr to get RSSI information
                leach_packet.rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
                LOG_INFO("Received a broadcast message with payload 'hello' from %02x:%02x (RSSI: %d)\n",
                         src->u8[0], src->u8[1], leach_packet.rssi);
            }
        } else {
            // Unicast message
            LOG_INFO("Received a unicast message from %02x:%02x to %02x:%02x\n",
                      src->u8[0], src->u8[1], dest->u8[0], dest->u8[1]);
        }
    }
}
bool should_be_cluster_head(int r){
    double probability = P / (1 - P * (r % (int)(1 / P)));
    // Generate a random number between 0 and 1
    double random_value = (double)rand() / RAND_MAX;
    LOG_INFO("random value: %f %f\n", probability, random_value);
    return random_value < probability;
}


/*---------------------------------------------------------------------------*/
// ... (previous code remains unchanged)

PROCESS_THREAD(leach_node_process, ev, data){
    static struct etimer adv_timer;
    static struct etimer round_timer;
    static struct etimer data_timer;

    static int r = 0;
    static int has_been_ch = 0;

    PROCESS_BEGIN();

    LOG_INFO("process begin\n");
    // Initialize NullNet
    nullnet_set_input_callback(input_callback);

    while(1) {
        // Node broadcasts an advertisement
        leach_packet.node_id = linkaddr_node_addr.u8[1];
        leach_packet.cluster_head = 0;  // Placeholder for cluster head information
        leach_packet.data_value = 0;    // Placeholder for data value
        leach_packet.energy_usage = 0; 

        nullnet_buf = (uint8_t *)&leach_packet;
        nullnet_len = sizeof(leach_packet);

        NETSTACK_NETWORK.output(NULL);
        LOG_INFO("set etimer for the round interval\n");

        // Set timer for initial advertisement
        etimer_set(&adv_timer, ADV_INTERVAL);

        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&adv_timer));
        LOG_INFO("inside the first while loop\n");

        // Set timer for next round
        etimer_set(&round_timer, ROUND_INTERVAL);

        while (1) {
            // Setup phase
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&round_timer));
            printf("Start of the round %d\n", r);

            if (leach_packet.cluster_head == 0 && has_been_ch > 0) {
                LOG_INFO("This node was ch before, so it is adding up has_been_ch\n");
                has_been_ch++;
            }
            if (has_been_ch > 1/P) {
                LOG_INFO("Resetting has_been_ch\n");
                has_been_ch = 0;
            }
            leach_packet.cluster_head = 0;

            // Node checks if it should be a cluster head for this round
            // Implement LEACH cluster head selection logic here
            if (r == 0 || has_been_ch == 0) {
                LOG_INFO("checking if this node could be a cluster head\n");
                leach_packet.cluster_head = should_be_cluster_head(r);
            }
            if (leach_packet.cluster_head) {
                listen_enabled = false;
                has_been_ch++;
                LOG_INFO("Node %u elected as a cluster head: round %d\n", leach_packet.node_id, r);

                // Send broadcast
                broadcast_message_t message;
                LOG_INFO("Broadcasting message\n");
                strncpy(message.data, "hello", sizeof(message.data));
                nullnet_buf = (uint8_t *)&message;
                nullnet_len = sizeof(message);

                LOG_INFO("Node %u sends a broadcast message %s: round %d\n", leach_packet.node_id, message.data, r);

                NETSTACK_NETWORK.output(NULL);
            } else {
                listen_enabled = true;
                etimer_set(&data_timer, DATA_INTERVAL);

                // Steady phase
                PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&data_timer));
                LOG_INFO("Inside Steady Phase\n");

                if (leach_packet.cluster_head == 1) {
                    LOG_INFO("Start listening\n");
                    listen_enabled = true;
                }

                // Node sends data to its cluster head
                if (leach_packet.cluster_head == 0) {
                    LOG_INFO("Stop listening because it is not a clusterhead and should send data to the cluster head\n");
                    //listen_enabled = false;
                    leach_packet.data_value = 42;
                    nullnet_buf = (uint8_t *)&leach_packet;
                    nullnet_len = sizeof(leach_packet);

                    LOG_INFO("Node %u sends data to cluster head %u: %u [round %d]\n",
                             leach_packet.node_id, leach_packet.cluster_head, leach_packet.data_value, r);

                    NETSTACK_NETWORK.output(NULL);

                    LOG_INFO("after broadcasting\n");
                }
            }

            // Reset data timer for the next data transmission
            etimer_reset(&data_timer);
            LOG_INFO("outside of the third loop\n");

            // Reset round timer for the next round
            etimer_reset(&round_timer);
            r++;
        }
        LOG_INFO("outside of the second loop\n");
    }
    LOG_INFO("outside of the first loop\n");

    PROCESS_END();
}

/*---------------------------------------------------------------------------*/