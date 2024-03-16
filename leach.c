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
//addition of all the intervals should not go over the round interval
#define ADV_INTERVAL (600 * CLOCK_SECOND)
#define ROUND_INTERVAL (10000 * CLOCK_SECOND)
#define DATA_INTERVAL (300 * CLOCK_SECOND)
#define P 0.05

/* Configuration */
#define SEND_INTERVAL (8 * CLOCK_SECOND)
static linkaddr_t dest_addr;

#if MAC_CONF_WITH_TSCH
#include "net/mac/tsch/tsch.h"
static linkaddr_t coordinator_addr;
#endif /* MAC_CONF_WITH_TSCH */

#define SLOT_DURATION (CLOCK_SECOND * 5)  // Adjust the slot duration as needed

/*---------------------------------------------------------------------------*/
// connect all processes
PROCESS(leach_process, "LEACH Process");

// process for deciding the cluster heads
PROCESS(ch_process, "Cluster Head Process");

// process for CH advertising(broadcasting)
PROCESS(adv_process, "Advertisement Process");

// process for non CHs unicasting back to CH
PROCESS(response_process, "Response Process");

// process for CH to create and send TDMA schedule
PROCESS(tdma_make_process, "Creating TDMA Process");

// process for non CHs to send data using TDMA
PROCESS(send_tdma_process, "Sending with TDMA Process");

// process for CH to do data fusion on the data sent and send it to the sink
PROCESS(data_fuse_process, "Data fusion Process");

AUTOSTART_PROCESSES(&leach_process);
/*---------------------------------------------------------------------------*/
// non cluster head uses this to keep neighbor information
struct NeighborInfo{
    linkaddr_t address;
    int16_t rssi;
};

// cluster head uses this to keep all the neighbor information and its tdma slots together
struct ClusterInfo{
    linkaddr_t address[100];
    uint8_t tdma_slot[100]; // TDMA slot assigned to the node
    uint8_t i_value;
};

static bool is_ch = false; // cluster head or not
static int r=0; // round number
// non cluster head
static struct NeighborInfo strongest_neighbor;
static bool new_broadcast_received = false;
static bool receive_tdma = false;
// cluster head
static struct ClusterInfo cluster_neighbor;
static int i=-1; // number of tdma_slots
uint8_t tdma; // tdma_slot

// callbacks for broadcast/unicast
void input_callback(const void *data, uint16_t len,
  const linkaddr_t *src, const linkaddr_t *dest)
{
    // if broadcast
    if(dest->u8[0] == 0){
        // only the non cluster heads receive the broadcast
        if(!is_ch){
            if(!receive_tdma){ // during the first advertisement period
                LOG_INFO("Adv: Received a broadcast message with payload from %02d\n",
                        src->u8[0]);
                int16_t rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
                if(new_broadcast_received == false){ // the very first packet sender will become the strongest neighbor
                    strongest_neighbor.address = *src;
                    strongest_neighbor.rssi = rssi;
                    new_broadcast_received = true;
                } else if (rssi > strongest_neighbor.rssi){ // compare with the packet sender later and keep the one with the strongest rssi signal
                    strongest_neighbor.address = *src;
                    strongest_neighbor.rssi = rssi;
                }
             } else if (linkaddr_cmp(src, &strongest_neighbor.address)){
                LOG_INFO("Rcv: Received a broadcast message with payload from %02d\n",
                        src->u8[0]);
                // during the tdma schedule broadcasting period
                // received packet is in the same form using the struct Cluster info
                struct ClusterInfo *receivedPacket = (struct ClusterInfo *)data;
                // take out the i value to check if the received packet is correct         
                uint8_t receivedIValue = receivedPacket->i_value;
                LOG_INFO("[%d]Received TDMA slot and i value %d from %02d\n", r, receivedIValue, src->u8[0]);
            }
        }
    } else { // unicast
        // only the cluster heads receive this
        if(is_ch){ // receive cluster join messages from the neighbors
            cluster_neighbor.address[++i] = *src;
            LOG_INFO("[%d]Received a unicast message with payload from %02d: %d\n", r, src->u8[0], i);
        }
    }
}

// function to check the probability of becoming the cluster head
bool should_be_cluster_head(int r){
    unsigned short random_value = random_rand();
    double probability = P / (1 - P * (r % (int)(1 / P)));
    double normalized_value = (double)random_value / RANDOM_RAND_MAX;
    LOG_INFO("random value: %f %f\n", probability, normalized_value);
    return normalized_value < probability;
}

// function to reset cluster information for the next round
void free_cluster_neighbor(){
    cluster_neighbor.i_value = 0;
    for(int j=0; j<i; j++){
        cluster_neighbor.address[j].u8[0] = 0;
        cluster_neighbor.address[j].u8[1] = 0;
        cluster_neighbor.tdma_slot[j] = 0;
    }
    i=-1;
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(leach_process, ev, data){
    // define the etimers
    static struct etimer round_timer; //for every one round
    static struct etimer adv_timer; // for the advertisement (broadcasting) by clusterheads
    static struct etimer data_timer; // for unicasting from non cluster heads
    
    static unsigned count = 0; // just an example of sending a data
    static int has_been_ch = 0; // check if the node has been a cluster head within 1/P rounds

    // start process
    PROCESS_BEGIN();
    LOG_INFO("process begin\n");

// set tsch? I don't understand this part, but without this, the unicast and the broadcast does not work
#if MAC_CONF_WITH_TSCH
  tsch_set_coordinator(linkaddr_cmp(&coordinator_addr, &linkaddr_node_addr));
#endif /* MAC_CONF_WITH_TSCH */

  /* Initialize NullNet */
    nullnet_buf = (uint8_t *)&count;
    nullnet_len = sizeof(count);
    nullnet_set_input_callback(input_callback);

    while(1) {
        LOG_INFO("Start:\n");
        etimer_set(&round_timer, ROUND_INTERVAL);
        // start round
        while(1){
            // reset round timer before going into another round
            LOG_INFO("round:%d\n", r);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&round_timer));
            //reset strongest neighbor information
            new_broadcast_recedived = false;
            receive_tdma = false;
            is_ch = false;
            
            //reset cluster information
            free_cluster_neighbor();

            // check whether the node has been a cluster head within the last 1/P rounds
            if(has_been_ch>0){
                has_been_ch++;
            }
            if(has_been_ch > 1/P){
                has_been_ch = 0;
            }

            // if fulfilling the condition, participate in choosing the clusterhead
            if(has_been_ch == 0){
               LOG_INFO("Choosing clusterhead\n");
               is_ch = should_be_cluster_head(r);
            }

            // advertisement phase:
            // cluster heads send broadcast
            if(is_ch){
                LOG_INFO("[%d]This node is CH\n", r);
                LOG_INFO("[%d]Sending out broadcast\n", r);
                memcpy(nullnet_buf, &count, sizeof(count));
                nullnet_len = sizeof(count);
                NETSTACK_NETWORK.output(NULL);
                has_been_ch++;
            }
            
            // wait for the broadcast to be received
            etimer_set(&adv_timer, ADV_INTERVAL);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&adv_timer));
            
            // Cluster Set-Up Phase:
            // non cluster heads send out unicast (Make the unicast CSMA)
            if(!is_ch){
                // make the destination the strongest neighbor cluster head
                dest_addr = strongest_neighbor.address;
                LOG_INFO("Strongest neighbor address: %02d:%02d\n", strongest_neighbor.address.u8[0], strongest_neighbor.address.u8[1]);
                
                // if a node received the broadcast
                if(new_broadcast_received){
                    // send a unicast to the CH
                    memcpy(nullnet_buf, &count, sizeof(count));
                    nullnet_len = sizeof(count);
                    LOG_INFO("[%d]sending out unicast to %d\n", r, dest_addr.u8[0]);
                    NETSTACK_NETWORK.output(&dest_addr);
                } else {
                    // taking out the ones that didn't receive a broadcast from the members of the cluster
                    LOG_INFO("[%d]doesn't belong in any cluster\n", r);
                }
            }
            LOG_INFO("[%d]finished unicasting\n", r);
        
            // wait for the unicast to be received
            etimer_set(&data_timer, DATA_INTERVAL);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&data_timer));

            // non cluster heads get ready to receive the tdma schedule broadcasting
            if(!is_ch){
                receive_tdma = true;
            }

            // cluster heads create TDMA schedule for their cluster members
            // broadcast the TDMA schedule
            if(is_ch){
                // insert the number of the cluster members into the cluster_neighbor structure
                cluster_neighbor.i_value = i;
                // check if the i values are correct
                LOG_INFO("[%d]TDMA i: %d\n", r, cluster_neighbor.i_value);

                for(int j=0; j<i; j++){
                    // create a tdma slot number for each cluster members
                    cluster_neighbor.tdma_slot[j] = j % i;
                    // check if the slot numbers and the ivalues are correct, corresponding to their addresses
                    //LOG_INFO("TDMA slot: %d, i value %d, destination %02d\n", cluster_neighbor.tdma_slot[j], cluster_neighbor.i_value, cluster_neighbor.address[j].u8[0]);
                }

                // send TDMA schedule using broadcast
                memcpy(nullnet_buf, &cluster_neighbor, sizeof(cluster_neighbor));
                nullnet_len = sizeof(cluster_neighbor);
                LOG_INFO("[%d] Broadcasting TDMA slot\n", r);
                NETSTACK_NETWORK.output(NULL);
            }         
        
            // wait for the broadcast to be received
            etimer_reset(&adv_timer);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&adv_timer));
        
            // check whether the broadcast has been received correctly
            // ⚠ there has been a problem here
            if(!is_ch){
                LOG_INFO("[%d]Cluster_neighbor: i value %d\n", r, cluster_neighbor.i_value);
            }
        
            //send out data using the tdma slot
            
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
            etimer_reset(&data_timer);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&data_timer));
*/
            //reset timer for the next round
            etimer_reset(&round_timer);
            etimer_reset(&adv_timer);
            etimer_reset(&data_timer);
            // add one to the round
            r++;
        }
    }
    PROCESS_END();
}

/*---------------------------------------------------------------------------*/