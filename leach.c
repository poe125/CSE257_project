//LOOK AT EACH "i" of IS_CH AND !IS_CH SO IT IS BEING HANDLED CORRECTLY
//PROBLEM HERE IS WHERE THE FOR LOOP IS ONLY BEING EXECUTED TWICE
//THE FOR LOOP IS BEING EXECUTED CORRECTLY BUT THE OTHER ONES ARE NOT.
//MIGHT BE THE PROBLEM THAT !IS_CH IS NOT GETTING OUT OF THE LOOP OR SOMETHING

#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "net/packetbuf.h"

#include <string.h>
#include <stdio.h> /* For printf() */
#include <stdlib.h>
#include <etimer.h>
#include <random.h>
#include <process.h>

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "LEACH_Node"
#define LOG_LEVEL LOG_LEVEL_INFO

/* Configuration */
#define ADV_INTERVAL (600 * CLOCK_SECOND)
#define ROUND_INTERVAL (6000 * CLOCK_SECOND)
#define DATA_INTERVAL (300 * CLOCK_SECOND)
#define TDMA_INTERVAL (1000 * CLOCK_SECOND)
#define P 0.05

/* Configuration */
#define SEND_INTERVAL (8 * CLOCK_SECOND)
static linkaddr_t dest_addr =         {{ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }};

#if MAC_CONF_WITH_TSCH
#include "net/mac/tsch/tsch.h"
static linkaddr_t coordinator_addr =  {{ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }};
#endif /* MAC_CONF_WITH_TSCH */

#define SLOT_DURATION (CLOCK_SECOND * 5)  // Adjust the slot duration as needed
/*---------------------------------------------------------------------------*/
PROCESS(leach_process, "LEACH Process");
AUTOSTART_PROCESSES(&leach_process);

/*---------------------------------------------------------------------------*/
//CH uses this to keep neighbor information
struct NeighborInfo{
    linkaddr_t address;
    int16_t rssi;
};

//CH uses this to keep all the neighbor information and its tdma slots together
struct ClusterInfo{
    linkaddr_t address[100];
    uint8_t tdma_slot[100]; // TDMA slot assigned to the node
};

//the data packet CH sends
struct DataPacket{
        uint8_t tdma_slot;
        uint8_t i_value;
};

struct NeighborInfo strongest_neighbor;
struct ClusterInfo cluster_neighbor;
struct DataPacket packet_data;
// linkaddr_t *strongest_ch;
static bool new_broadcast_received, receive_nearest_neighbor = false;
static int i=0;
static bool is_ch = false;
uint8_t tdma;
static int r = 0;

void input_callback(const void *data, uint16_t len,
  const linkaddr_t *src, const linkaddr_t *dest)
{
    //if broadcast
    if(dest->u8[0] == 0){
        LOG_INFO("Received a broadcast message with payload from %02d\n",
                     src->u8[0]);
                     
        int16_t rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
        if(new_broadcast_received == false){
            strongest_neighbor.address = *src;
            strongest_neighbor.rssi = rssi;
        } else if (rssi > strongest_neighbor.rssi){
            strongest_neighbor.address = *src;
            strongest_neighbor.rssi = rssi;
        }
        new_broadcast_received = true;
    } else { //unicast
        if(is_ch && receive_nearest_neighbor){//get information about the nearest neighbors
            LOG_INFO("[%d]Received a unicast message with payload from %02d\n", r, src->u8[0]);
            cluster_neighbor.address[i] = *src;
            i++;
        } 
        if (!is_ch){//get information about tdma
            struct DataPacket *receivedPacket = (struct DataPacket *)data;
            // Now you can access the fields of the received packet
            //is somehow only received by the first two nodes that are being sent
            uint8_t receivedTdmaSlot = receivedPacket->tdma_slot;
            uint8_t receivedIValue = receivedPacket->i_value;
            LOG_INFO("[%d]Received TDMA slot %d and i value %d\n", r, receivedTdmaSlot, receivedIValue);
        }
    }
}

bool should_be_cluster_head(int r){
    unsigned short random_value = random_rand();
    double probability = P / (1 - P * (r % (int)(1 / P)));
    double normalized_value = (double)random_value / RANDOM_RAND_MAX;
    LOG_INFO("random value: %f %f\n", probability, normalized_value);
    return normalized_value < probability;
}

void free_cluster_neighbor(){
    for(int j=0; j<i; j++){
        cluster_neighbor.address[j].u8[0] = 0;
    }
    i=0;
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(leach_process, ev, data){
    static struct etimer adv_timer;
    static struct etimer round_timer;
    static struct etimer data_timer;
    static struct etimer tdma_timer;
    
    static unsigned count = 0;
    static int has_been_ch = 0;

    PROCESS_BEGIN();

    LOG_INFO("process begin\n");

#if MAC_CONF_WITH_TSCH
  tsch_set_coordinator(linkaddr_cmp(&coordinator_addr, &linkaddr_node_addr));
#endif /* MAC_CONF_WITH_TSCH */

  /* Initialize NullNet */
    nullnet_buf = (uint8_t *)&count;
    nullnet_len = sizeof(count);
    nullnet_set_input_callback(input_callback);

    while(1) {
        //advertisement phase
        LOG_INFO("Start:\n");
        etimer_set(&round_timer, ROUND_INTERVAL);
        while(1){
            LOG_INFO("round:%d\n", r);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&round_timer));
            //choose cluster head
            etimer_set(&adv_timer, ADV_INTERVAL);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&adv_timer));
            if(!is_ch && has_been_ch>0){
                has_been_ch++;
            }

            if(has_been_ch > 1/P){
                has_been_ch = 0;
            }
            is_ch = false;

            if(has_been_ch == 0){
               LOG_INFO("Choosing clusterhead\n");
               is_ch = should_be_cluster_head(r);
            }

            //sending broadcast
            if(is_ch){
                LOG_INFO("[%d]This node is CH\n", r);
                LOG_INFO("[%d]Sending out broadcast\n", r);
                has_been_ch++;
                memcpy(nullnet_buf, &count, sizeof(count));
                nullnet_len = sizeof(count);
                NETSTACK_NETWORK.output(NULL);
                count++;
                receive_nearest_neighbor = true;
            }
            
            //wait for the broadcast to be received
            etimer_set(&data_timer, DATA_INTERVAL);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&data_timer));
            
            //Cluster Set-Up Phase
            //sending unicast
            //all broadcast is received but not all unicast is received
            if(!is_ch){
                dest_addr = strongest_neighbor.address;
                if(dest_addr.u8[0] == 0){
                    //I'm just taking out the ones that didn't receive a broadcast from the clusterhead
                    LOG_INFO("[%d]doesn't belong in any cluster\n", r);
                } else {
                    //this part gets less and less as the r counter goes on.
                    memcpy(nullnet_buf, &count, sizeof(count));
                    nullnet_len = sizeof(count);
                    LOG_INFO("[%d]sending out unicast to %d\n", r, dest_addr.u8[0]);
                    //send a unicast
                    NETSTACK_NETWORK.output(&dest_addr);
                    count++;  
                }
                LOG_INFO("[%d]finished unicasting\n", r);
            }

            //wait for the unicast to be received
            etimer_set(&adv_timer, ADV_INTERVAL);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&adv_timer));
            
            // cluster heads create TDMA for their cluster members
            
            if(is_ch){
                int j=0;
                do{
                    LOG_INFO("TDMA i: %d\n", i);
                    cluster_neighbor.tdma_slot[j] = j % i;
                    packet_data.tdma_slot = cluster_neighbor.tdma_slot[j];
                    LOG_INFO("tdma slot: %d\n",packet_data.tdma_slot);
                    packet_data.i_value = i;
                    memcpy(nullnet_buf, &packet_data, sizeof(packet_data));
                    nullnet_len = sizeof(packet_data);
                    dest_addr = cluster_neighbor.address[j];
                    LOG_INFO("[%d] Unicasting TDMA slot %d and i value %d to node %02d:\n", r, packet_data.tdma_slot, packet_data.i_value, dest_addr.u8[0]);
                    NETSTACK_NETWORK.output(&dest_addr);
                    LOG_INFO("[%d] TDMA is_ch: %d\n", r, is_ch);
                    j++;
                    //it shows only 0 and 2 if I do j+=2 WHY
                }while(j<i);
            }

            // etimer_reset(&adv_timer);
            // PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&adv_timer));
            // LOG_INFO("[%d]Still waiting for tdma timer\n", r);
            etimer_set(&tdma_timer, TDMA_INTERVAL);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&tdma_timer));
            LOG_INFO("[%d]Finished waiting for tdma timer\n", r);
            //start sending using the tdma slots
            count = 0;
            receive_nearest_neighbor = false;
            /*uint8_t current_time_slot;
            
            if(!is_ch){
                    // Wait for the next time slot
                    etimer_set(&tdma_timer, TDMA_INTERVAL);
                    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&tdma_timer));

                    LOG_INFO("NonCH: TDMA slot %d and i value %d\n", packet_data.tdma_slot, packet_data.i_value);

                    // Check if it's the node's time slot
                    if (current_time_slot == packet_data.tdma_slot)
                    {
                        memcpy(nullnet_buf, &count, sizeof(count));
                        nullnet_len = sizeof(count);
                        LOG_INFO("[%d]sending out unicast to %02d during TDMA slot %d\n", r, dest_addr.u8[0], packet_data.tdma_slot);
                        NETSTACK_NETWORK.output(&dest_addr);
                        count++;
                        break;
                    }

                    // Increment the time slot counter
                    current_time_slot = (current_time_slot + 1) % packet_data.i_value;
                    etimer_reset(&tdma_timer);
                }
*/
            etimer_reset(&data_timer);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&data_timer));

            r++;
            //reset strongest neighbor information
            new_broadcast_received = false;
            //reset cluster information
            free_cluster_neighbor(cluster_neighbor);
            etimer_reset(&data_timer);
            etimer_reset(&round_timer);
        }
    }
    PROCESS_END();
}

/*---------------------------------------------------------------------------*/