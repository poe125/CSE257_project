#include <contiki.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <random.h>
#include <etimer.h>
#include "../contiki-2.7/core/net/rime.h"
#include "../contiki-2.7/core/net/rime/broadcast.h"
#include "../contiki-2.7/core/net/rime/unicast.h"
#include "../contiki-2.7/core/net/mac/rdc.h"
#include "../contiki-2.7/core/net/netstack.h"
#include "../contiki-2.7/core/net/rime/collect.h"

#define MAX_UNICAST_NODES 100
#define UNICAST_EVENT  (PROCESS_EVENT_MAX + 1)

/*---------------------------------------------------------------------------*/
PROCESS(leach_process, "start leach process");
PROCESS(broadcast_process, "broadcast process");
PROCESS(unicast_process, "unicast process");
AUTOSTART_PROCESSES(&leach_process, &broadcast_process, &unicast_process);
/*---------------------------------------------------------------------------*/
static int r = 0; //round
static float p = 0.1; //probability of being CHs
static bool is_ch = false;

struct NeighborInfo {
    rimeaddr_t address;
    int16_t rssi;
};

struct NeighborInfo strongest_neighbor;
static bool new_broadcast_received = false;
rimeaddr_t strongest_cluster_head;

/*---------------------------------------------------------------------------*/
//BROADCAST//
// Define the broadcast receive callback function
static void broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from) {
    const char *received_message = (char *)packetbuf_dataptr();
    printf("Received broadcast message from %d.%d: '%s'\n", from->u8[0], from->u8[1], received_message);    
    
    // Get the RSSI value from the collect_neighbor structure
    int16_t rssi_value = packetbuf_attr(PACKETBUF_ATTR_RSSI);
    // Update information about the node with the strongest signal
    if(new_broadcast_received == false){
        strongest_neighbor.address = *from;
        strongest_neighbor.rssi = rssi_value;
    } else {
        if (rssi_value > strongest_neighbor.rssi) {
            strongest_neighbor.address = *from;
            strongest_neighbor.rssi = rssi_value;
        }
    }
    //store information about the strongest cluster head
    rimeaddr_copy(&strongest_cluster_head, from);
    printf("Strongest Signal: Node %d.%d (RSSI: %d)\n", strongest_neighbor.address.u8[0], strongest_neighbor.address.u8[1], strongest_neighbor.rssi);
    new_broadcast_received = true;
}

// Configure the broadcast connection
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;

/*---------------------------------------------------------------------------*/
//UNICAST
static rimeaddr_t received_nodes[MAX_UNICAST_NODES];
static uint8_t num_received_nodes = 0;

//update the list of received nodes
static void update_received_nodes_list(const rimeaddr_t node) {
    uint8_t i;
    // Check if the node is already in the list
    for (i = 0; i < num_received_nodes; i++) {
      if (rimeaddr_cmp(&received_nodes[i], &node)) {
        return;  // Node is already in the list
      }
    }
    // Add the node to the list
    if (num_received_nodes < MAX_UNICAST_NODES) {
      rimeaddr_copy(&received_nodes[num_received_nodes], &node);
      num_received_nodes++;
    } else {
      printf("Max number of nodes reached in the list.\n");
    }
}
// Unicast receive callback function
static void recv_uc(struct unicast_conn *c, const rimeaddr_t *from) {
  const char *received_message = (char *)packetbuf_dataptr();
  printf("Unicast message received from %d.%d: '%s'\n", from->u8[0], from->u8[1], received_message);
  update_received_nodes_list(*from);
}

// Unicast connection callbacks
static const struct unicast_callbacks unicast_call = {recv_uc};
static struct unicast_conn unicast;

uint8_t i;
static void print_received_nodes_list() {
  printf("List of received nodes:\n");
  for (i = 0; i < num_received_nodes; i++) {
    printf("Node %d.%d\n", received_nodes[i].u8[0], received_nodes[i].u8[1]);
  }

}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(unicast_process, ev, data) {
    PROCESS_EXITHANDLER(unicast_close(&unicast);)
    PROCESS_BEGIN();

    // Open the unicast connection
    unicast_open(&unicast, 146, &unicast_call);

    while (1) {
        PROCESS_WAIT_EVENT();
    }

    PROCESS_END();
}

PROCESS_THREAD(broadcast_process, ev, data) {
    PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
    PROCESS_BEGIN();

    // Open the broadcast connection
    broadcast_open(&broadcast, 129, &broadcast_call);

    while (1) {
        PROCESS_WAIT_EVENT();
    }

    PROCESS_END();
}
/*---------------------------------------------------------------------------*/
static void set_up_phase(){
    //select CH using p
    if(r==0){
        int rand_num = random_rand();
        is_ch = (rand_num < p * RANDOM_RAND_MAX / 100);
    } else if(r != 0 && !is_ch){
        int rand_num = random_rand();
        is_ch = (rand_num < p * RANDOM_RAND_MAX / 100);
    } else {
        is_ch = false;
    }

    if(is_ch){//CLUSTER HEAD
        printf("Round %d: I am a Cluster Head!\n", r);
        packetbuf_copyfrom("Hello", strlen("Hello") + 1);  // send this message using the broadcast
        broadcast_send(&broadcast);
        //random selection of CH if there are too many-> how?
        } else {//NOT CLUSTER HEAD
        printf("Round %d: I am not a Cluster Head.\n", r);
        //keep the receiver on
        NETSTACK_RDC.off(0);
        //choose cluster based on the minimum energy required to transit/receive messages/data
        //select the node that gave out the strongest message
        rimeaddr_copy(&strongest_neighbor.address, &strongest_cluster_head);
    }
}
/*---------------------------------------------------------------------------*/
static void steady_phase(){
    printf("Steady Phase: Strongest Neighbor: %d.%d\n", strongest_neighbor.address.u8[0], strongest_neighbor.address.u8[1]);
    //choose the CH again

    if(!is_ch){ //NOT CLUSTER HEAD
        //inform themselves to CH using unicast
        printf("Steady Phase: I am not a Cluster Head.\n");
        packetbuf_copyfrom("Return msg", strlen("Return msg") + 1);
        unicast_send(&unicast, &strongest_neighbor.address);
        process_post(&unicast_process, UNICAST_EVENT, NULL);
    } else { //CLUSTER HEAD
        NETSTACK_RDC.off(0);
        printf("Steady Phase: I am a Cluster Head!\n");
        //create a list of members in clusters
        print_received_nodes_list();
        //schedule communication of non-CH nodes based on TDMA

        //transmitter turned off for non-CHs when it's not sending
        //NETSTACK_RDC.off(1);
        //data aggregation after collecting data from non-CHs
        //CH trasmit the same to Base Station
    }
    /*if(new_broadcast_received){
        //reset the flag
        new_broadcast_received = false;
    }*/
}


/*---------------------------------------------------------------------------*/
//START COMMUNICATION
static struct etimer timer;
PROCESS_THREAD(leach_process, ev, data){
    PROCESS_BEGIN();

    while (1)
    {
        etimer_set(&timer, CLOCK_SECOND * 2);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer)); 
        // LEACH protocol logic
        // Periodically perform clustering, elect cluster heads, etc.
        set_up_phase();
        // Send and receive data within clusters
        // LEACH-specific code
        //Steady Phase logic
        steady_phase();
        r++;
        PROCESS_YIELD();
    }
    PROCESS_END();
}
/*---------------------------------------------------------------------------*/

