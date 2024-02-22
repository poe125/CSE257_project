#include <contiki.h>
#include <stdio.h>

//create 100 of these nodes
/*---------------------------------------------------------------------------*/
PROCESS(leach_process, "start leach process");
AUTOSTART_PROCESS(&leach_process);
/*---------------------------------------------------------------------------*/
//create classes, functions
static int r = 0; //round
static float p = 0.1; //probability of being CHs
static bool is_ch = false;
//if r = 0
static void set_up_phase(){
    //select CH using p
    int rand_num = random_rand();
    is_ch = (rand_num < p * RANDOM_RAND_MAX);

    if(is_ch){
        //is cluster head
        printf("Round %d: I am a Cluster Head!\n", r);
    } else {
        //is not cluster head
        //broadcast "Hello" in Cluster
        //the amount of energy used here is the same for all nodes
        //keep the receiver on
        //choose cluster based on the minimum energy required to transig/receive messages/data
        printf("Round %d: I am not a Cluster Head.\n", r);
    }
}

static void steady_phase(){
    r++;
    if(is_ch){
        //create a list of members in clusters
        //schedule communication of non-CH nodes based on TDMA
        //transmitter turned off for non-CHs when it's not sending
        //data aggregation after collecting data from non-CHs
        //CH trasmit the same to Base Station
        printf("Steady Phase: I am a Cluster Head!\n");
    } else {
        //inform themselves to CH
        printf("Steady Phase: I am not a Cluster Head.\n");
    }
}

static struct etimer timer;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(leach_process, ev, data){
    PROCESS_BEGIN();

    while (1)
    {
        etimer_set(&timer, CLOCK_SECOND);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
        // LEACH protocol logic

        // Periodically perform clustering, elect cluster heads, etc.
        set_up_phase();

        // Send and receive data within clusters

        // LEACH-specific code
        
        //Steady Phase logic
        steady_phase();

        PROCESS_YIELD();
    }
    PROCESS_END();
}
/*---------------------------------------------------------------------------*/