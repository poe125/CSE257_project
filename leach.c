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
#define WAIT_INTERVAL (1000 * CLOCK_SECOND)
#define ROUND_INTERVAL (6000 * CLOCK_SECOND)
#define DATA_INTERVAL (300 * CLOCK_SECOND)
#define P 0.5
#define MAX_NODE 3
#define ADDR_LENGTH sizeof(linkaddr_t)

/* Configuration */
#define SEND_INTERVAL (8 * CLOCK_SECOND)
static linkaddr_t dest_addr;

#if MAC_CONF_WITH_TSCH
#include "net/mac/tsch/tsch.h"
static linkaddr_t coordinator_addr;
#endif /* MAC_CONF_WITH_TSCH */

#define SLOT_DURATION (CLOCK_SECOND * 5)  // Adjust the slot duration as needed

/*---------------------------------------3
------------------------------------*/
static linkaddr_t TDMA_list[MAX_NODE];
static bool is_ch = false; 
static int round = 0; 
static int round_ch = -1/P;
// non cluster head
static bool new_broadcast_received = false;
static linkaddr_t strongest_neighbor;

// cluster head
static int cluster_size = -1;

/*---------------------------------------------------------------------------*/
PROCESS(leach_process, "LEACH Process");
PROCESS(ch_process, "Choosing cluster head");
PROCESS(adv_process, "Advertisement Process");
PROCESS(response_process, "Response Process");
PROCESS(tdma_make_process, "Creating TDMA Process");
PROCESS(send_tdma_process, "Sending with TDMA Process");
PROCESS(data_fuse_process, "Data fusion Process");

PROCESS(wait_for_broadcast_process, "Receive Data Process");
PROCESS(free_process, "freeing process");

AUTOSTART_PROCESSES(&leach_process);

/*---------------------------------------------------------------------------*/
// callbacks for broadcast/unicast
void input_callback(const void *data, uint16_t len,
  const linkaddr_t *src, const linkaddr_t *dest)
{
    if(dest->u8[0] == 0 && dest->u8[1] == 0){ //broadcast
        if(!is_ch){
            LOG_INFO("[%d] Received broadcast from %d\n", round, src->u8[0]);
            strongest_neighbor = *src;
            new_broadcast_received = true;
        }
    } else { //unicast
        if(is_ch){
            LOG_INFO("[%d] Received unicast from %d\n", round, src->u8[0]);
        }
    }
}

// function to check the probability of becoming the cluster head
bool decide_cluster_head(int r){
    unsigned short random_value = random_rand();
    double probability = P / (1 - P * (r % (int)(1 / P)));
    double normalized_value = (double)random_value / RANDOM_RAND_MAX;
    LOG_INFO("random value: %f %f\n", probability, normalized_value);
    return normalized_value < probability;
}

//serialize the TDMA_list into a byte array
void serialize_TDMA_list(linkaddr_t *TDMA_list, uint8_t *serialized_list, int num_nodes)
{
    for (int i = 0; i < num_nodes; i++)
    {
        memcpy(&serialized_list[i * ADDR_LENGTH], &TDMA_list[i], ADDR_LENGTH);
    }
}

// function to reset cluster information for the next round
void free_array(){
    for(int j=0; j<MAX_NODE; j++){
        TDMA_list[j].u8[0] = 0;
        TDMA_list[j].u8[1] = 0;
    }
    cluster_size=-1;
}

/*---------------------------------------------------------------------------*/

//the main process for running the leach protocol
PROCESS_THREAD(leach_process, ev, data){
    // define the etimers
    static struct etimer round_timer; //for every one round
    static struct etimer adv_leach_timer;
    static struct etimer data_leach_timer;
    PROCESS_BEGIN();

    LOG_INFO("Round_ch%d and round:%d\n", round_ch, round);
// set tsch? I don't understand this part, but without this, the unicast and the broadcast does not work
#if MAC_CONF_WITH_TSCH
  tsch_set_coordinator(linkaddr_cmp(&coordinator_addr, &linkaddr_node_addr));
#endif /* MAC_CONF_WITH_TSCH */

    /* Initialize NullNet */
    nullnet_set_input_callback(input_callback);

    etimer_set(&round_timer, ROUND_INTERVAL);
    
    while(1){
        PROCESS_WAIT_UNTIL(etimer_expired(&round_timer));

        // Start the threads
        process_start(&ch_process, NULL);
        // LOG_INFO("[%d] Cluster head decided: %d\n", round, is_ch);
        
        LOG_INFO("[%d] is_ch: %d\n", round, is_ch);
        if(is_ch){
            process_start(&adv_process, NULL);
            round_ch = round;
            // LOG_INFO("[%d] Sent out advertisements\n", round);
        }

        if(!is_ch){
            process_start(&wait_for_broadcast_process, NULL);
        }
        
        etimer_set(&data_leach_timer, DATA_INTERVAL);
        PROCESS_WAIT_UNTIL(etimer_expired(&data_leach_timer));
        
        
        
        if(!is_ch){
            process_start(&response_process, NULL);
            // LOG_INFO("[%d] Sent back unicast\n", round);
        }
        
        etimer_set(&adv_leach_timer, ADV_INTERVAL);
        PROCESS_WAIT_UNTIL(etimer_expired(&adv_leach_timer));


        if(is_ch){
            process_start(&tdma_make_process, NULL);
            LOG_INFO("[%d] Sent out tdma schedule \n", round);
        }

        etimer_reset(&adv_leach_timer);
        PROCESS_WAIT_UNTIL(etimer_expired(&adv_leach_timer));
        
        if(!is_ch){
            process_start(&send_tdma_process, NULL);
        }
        
        etimer_reset(&adv_leach_timer);
        PROCESS_WAIT_UNTIL(etimer_expired(&adv_leach_timer));

        if(is_ch){
            process_start(&data_fuse_process, NULL);
        }

        etimer_reset(&adv_leach_timer);
        PROCESS_WAIT_UNTIL(etimer_expired(&adv_leach_timer));

        // reset timer
        etimer_reset(&round_timer);
        etimer_reset(&adv_leach_timer);
        etimer_reset(&data_leach_timer);

        process_start(&free_process, NULL);
        
        // reset cluster information

    }
    PROCESS_END();
}
/*---------------------------------------------------------------------------*/
// decide the cluster heads
PROCESS_THREAD(ch_process, ev, data){
    PROCESS_BEGIN();
    LOG_INFO("[%d] Cluster head process\n", round);
    // check whether the node has been a cluster head within the last 1/P rounds
    // if fulfilling the condition, participate in choosing the clusterhead
    
    if(round_ch + 1/P <= round){
        is_ch = decide_cluster_head(round);
    }

    PROCESS_END();
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(wait_for_broadcast_process, ev, data){
    static struct etimer wait_timer;
    PROCESS_BEGIN();

    LOG_INFO("[%d] Waiting for messages...\n", round);
    etimer_set(&wait_timer, WAIT_INTERVAL);
    
    while (1) {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&wait_timer));
        etimer_reset(&wait_timer);
    }

    PROCESS_END();
}


/*---------------------------------------------------------------------------*/
PROCESS_THREAD(adv_process, ev, data){
    // define the etimers
    PROCESS_BEGIN();
    // to wait for another 1/P rounds to become a cluster head
    
    LOG_INFO("[%d] Sending out broadcast\n", round);
    uint8_t serialized_list[MAX_NODE * ADDR_LENGTH];
    serialize_TDMA_list(TDMA_list, serialized_list, MAX_NODE);
    nullnet_buf = serialized_list;
    nullnet_len = MAX_NODE * ADDR_LENGTH;
    NETSTACK_NETWORK.output(NULL);
    PROCESS_END();
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(response_process, ev, data){
    // define the etimers
    PROCESS_BEGIN();
    LOG_INFO("[%d] Sending unicast response\n", round);
    dest_addr = strongest_neighbor;

    // if a node received the broadcast
    if(new_broadcast_received){
        // send a unicast to the CH
        uint8_t serialized_list[MAX_NODE * ADDR_LENGTH];
        serialize_TDMA_list(TDMA_list, serialized_list, MAX_NODE);
        NETSTACK_NETWORK.output(&dest_addr);
        LOG_INFO("[%d] sent out unicast to %d\n", round, dest_addr.u8[0]);
    } else {
        // taking out the ones that didn't receive a broadcast from the members of the cluster
        LOG_INFO("[%d] doesn't belong in any cluster\n", round);
    }
    
    PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(tdma_make_process, ev, data){
    // define the etimers
    
    PROCESS_BEGIN();
    LOG_INFO("[%d] Sending TDMA schedule\n", round);
    // send TDMA schedule using broadcast
    uint8_t serialized_list[MAX_NODE * ADDR_LENGTH];
    serialize_TDMA_list(TDMA_list, serialized_list, MAX_NODE);
    NETSTACK_NETWORK.output(NULL); 
    PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(send_tdma_process, ev, data){
    // define the etimers
    // static struct etimer adv_timer; // for the advertisement (broadcasting) by clusterheads

    PROCESS_BEGIN();

    LOG_INFO("[%d] Sending with TDMA process\n", round);
    uint8_t serialized_list[MAX_NODE * ADDR_LENGTH];
    serialize_TDMA_list(TDMA_list, serialized_list, MAX_NODE);
    nullnet_buf = serialized_list;
    nullnet_len = MAX_NODE * ADDR_LENGTH;
    NETSTACK_NETWORK.output(&dest_addr);
        
    PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(data_fuse_process, ev, data){
    // define the etimers
    // static struct etimer adv_timer; // for the advertisement (broadcasting) by clusterheads
    PROCESS_BEGIN();
    LOG_INFO("[%d] Sending fused data\n", round);
    uint8_t serialized_list[MAX_NODE * ADDR_LENGTH];
    serialize_TDMA_list(TDMA_list, serialized_list, MAX_NODE);
    nullnet_buf = serialized_list;
    nullnet_len = MAX_NODE * ADDR_LENGTH;
    NETSTACK_NETWORK.output(NULL);
    PROCESS_END();
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(free_process, ev, data){
    PROCESS_BEGIN();

    LOG_INFO("[%d] Round_ch: %d\n",round, round_ch);

    LOG_INFO("[%d] Freeing cluster neighbor information\n", round);
    
    // reset parameters
    free_array();
    round++;
    new_broadcast_received = false;
    is_ch = false;

    PROCESS_END();
}