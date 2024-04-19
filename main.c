#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "main.h"

pthread_mutex_t outputLock, statsLock, transactionLock;
pthread_mutex_t cookPriorityLock, ovenPriorityLock, packerPriorityLock, delivererPriorityLock;
pthread_mutex_t availableCookLock, availableOvenLock, availablePackerLock, availableDelivererLock;

pthread_cond_t cookPriorityCond, ovenPriorityCond, packerPriorityCond, delivererPriorityCond;
pthread_cond_t availableCookCond, availableOvenCond, availablePackerCond, availableDelivererCond;

int initialSeed;
int failedTransactionCount = 0;
int succesfulTransactionCount = 0;
int totalSpecialSold = 0;
int totalPlainSold = 0;
int totalIncome = 0;

int availableCookCount = N_COOK;
int availableOvenCount = N_OVEN;
int availablePackerCount = N_PACKER;
int availableDelivererCount = N_DELIVERER;

int priority = 1;
int cookPriority = 1;
int ovenPriority = 1;
int packerPriority = 1;
int delivererPriority = 1;

double maxOrderCompletionTime = 0;
double maxCoolingTime = 0;
double orderCompletionTimeSum = 0;
double coolingTimeSum = 0;

typedef struct customerOrder{
    int totalPizzas;
    int plainPizzas;
} customerOrder;


// Initializes mutex, prints message if initialization failed and exits with code -1. Used in main() only.
void initializeMutex(pthread_mutex_t *mutex){
    if (pthread_mutex_init(mutex, NULL) != 0){
        printf("ERROR: pthread_mutex_init() failed in main()\n");
        exit(-1);
    }
}

// Initializes conds, prints message if initialization failed and exits with code -1. Used in main() only.
void initializeCondition(pthread_cond_t *cond){
    if (pthread_cond_init(cond, NULL) != 0){
        printf("ERROR: pthread_cond_init() failed in main()\n");
        exit(-1);
    }
}

// Acquires lock, if acquisition fails then id of thread is printed and program exits.
void acquireLock(pthread_mutex_t *mutex, int oid, void* t){
    if (pthread_mutex_lock(mutex) != 0){
        printf("ERROR: pthread_mutex_lock() failed in thread %d\n", oid);
        exit(t);
    }
}

// Releases lock, if release fails then id of thread is printed and program exits.
void releaseLock(pthread_mutex_t *mutex, int oid, void* t){
    if (pthread_mutex_unlock(mutex) != 0){
        printf("ERROR: pthread_mutex_unlock() failed in thread %d\n", oid);
        exit(t);
    }
}

// Destroys lock, if destruction fails it prints a message and exits with code -1. Used in main() only.
void destroyLock(pthread_mutex_t *mutex){
    if (pthread_mutex_destroy(mutex) != 0){
        printf("ERROR: pthread_mutex_destroy() failed in main()\n");
    } 
}

// Destroys cond, if destruction fails it prints a message and exits with code -1. Used in main() only.
void destroyCond(pthread_cond_t *cond){
    if (pthread_cond_destroy(cond) != 0){
        printf("ERROR: pthread_cond_destroy() failed in main()\n");
    } 
}


// Returns 1 with a probability of p and 0 with a probability of 1-p.
int bernoulliDistr(float p, void* arg){

    unsigned int* seed = (unsigned int*) arg;
    // uniE(Uniform[0,1])
    double uni = (double) rand_r(seed) / RAND_MAX;

    if (uni < p) return 1;

    return 0;
}


void *simulateServiceFunc(void *t){

    struct timespec timeStarted, timeFinishedBaking, timeFinishedPacking, timeDelivered; 
    int orderPriority, wait;
    int* oid = (int *) t; 
    customerOrder order;

    // Seed for thread to use rand_r().
    unsigned int seed = initialSeed + *oid;

    // Get starting time.
    clock_gettime(CLOCK_REALTIME, &timeStarted);

    /* PART 1*/

    // Get total number of pizzas to order.
    order.totalPizzas = (rand_r(&seed) % (N_ORDER_HIGH - N_ORDER_LOW + 1)) + N_ORDER_LOW;

    order.plainPizzas = 0;
    // Get number of special pizzas.
    int i;
    for (i = 0; i < order.totalPizzas; i++){
        if (bernoulliDistr(P_PLAIN, &seed)){
            order.plainPizzas++;
        }
    }
    
    /* END OF PART 1*/


    /* PART 2*/

    // Simulate time to attempt transaction.
    wait = (rand_r(&seed) % (T_PAYMENT_HIGH - T_PAYMENT_LOW + 1)) + T_PAYMENT_LOW;
    sleep(wait);

    // Acquire lock to update transaction counts and count of pizzas variables.
    acquireLock(&transactionLock, *oid, t);
    
    // Acquire print lock to notify about transaction success/failure.
    acquireLock(&outputLock, *oid, t);

    // With probability P_FAIL transaction fails.
    if (bernoulliDistr(P_FAIL, &seed)){
        printf("Transaction with ID %d failed.\n", *oid);
        failedTransactionCount += 1;

        // Release transaction lock.
        releaseLock(&transactionLock, *oid, t);
        
        // Release print lock.
        releaseLock(&outputLock, *oid, t);
        
        // Terminate thread and release resources.
        pthread_exit(NULL);

    } else {
        printf("Transaction with ID %d succesful.\n", *oid);

        // Release print lock.
        releaseLock(&outputLock, *oid, t);

        totalPlainSold += order.plainPizzas;
        totalSpecialSold += (order.totalPizzas - order.plainPizzas);
        totalIncome += C_PLAIN * order.plainPizzas + C_SPECIAL *  (order.totalPizzas - order.plainPizzas);
        succesfulTransactionCount += 1;

        // Release transaction lock.
        releaseLock(&transactionLock, *oid, t);

        orderPriority = priority++; // assign order priority id to order, happens here because failed orders should not receive one.
    }
        
    /* END OF PART 2*/


    /* START OF PART 3*/
    
    // Enforce FCFS policy.
    acquireLock(&cookPriorityLock, *oid, t);
    while(cookPriority != orderPriority){
        pthread_cond_wait(&cookPriorityCond, &cookPriorityLock);
    }
    
    // Now get a cook.
    acquireLock(&availableCookLock, *oid, t);

    while(availableCookCount == 0){
        pthread_cond_wait(&availableCookCond, &availableCookLock);
    }
    availableCookCount -= 1;
    releaseLock(&availableCookLock, *oid, t);
    cookPriority += 1;
    releaseLock(&cookPriorityLock, *oid, t);
    pthread_cond_broadcast(&cookPriorityCond); // Notify other threads that they can "compete" for a cook. (Obv. thread with higher priority)

    // Simulate pizza preparation time.
    sleep(T_PREP * order.totalPizzas);

    /* END OF PART 3*/


    /* START OF PART 4 */
    
    // Enforce FCFS policy.
    acquireLock(&ovenPriorityLock, *oid, t);
    while(orderPriority != ovenPriority){
        pthread_cond_wait(&ovenPriorityCond, &ovenPriorityLock);
    }

    // Get enough ovens. (one for each pizza ordered)
    acquireLock(&availableOvenLock, *oid, t);

    // May re-wait multiple times.
    while(availableOvenCount < order.totalPizzas){
        pthread_cond_wait(&availableOvenCond, &availableOvenLock);
    }
    availableOvenCount -= order.totalPizzas;
    releaseLock(&availableOvenLock, *oid, t);

    ovenPriority += 1;
    releaseLock(&ovenPriorityLock, *oid, t);
    pthread_cond_broadcast(&ovenPriorityCond); // Notify other threads that they can "compete" for ovens. (Obv. thread with higher priority)
    
    // After ovens have been acquired, release cook.
    acquireLock(&availableCookLock, *oid, t);
    availableCookCount += 1;
    releaseLock(&availableCookLock, *oid, t);
    pthread_cond_signal(&availableCookCond); // At most one thread is waiting for cook at each point in time. (FCFS Policy)

    // Simulate baking time.
    sleep(T_BAKE);

    // Get time when baking finished.
    clock_gettime(CLOCK_REALTIME, &timeFinishedBaking);

    /* END OF PART 4 */


    /* START OF PART 5 */

    // Enforce FCFS policy.
    acquireLock(&packerPriorityLock, *oid, t);
    while(orderPriority != packerPriority){
        pthread_cond_wait(&packerPriorityCond, &packerPriorityLock); 
    }

    acquireLock(&availablePackerLock, *oid, t);
    while (availablePackerCount == 0){
        pthread_cond_wait(&availablePackerCond, &availablePackerLock);
    }
    availablePackerCount -= 1;
    releaseLock(&availablePackerLock, *oid, t);

    // Once packer has been acquired.
    packerPriority += 1;
    releaseLock(&packerPriorityLock, *oid , t);
    pthread_cond_broadcast(&packerPriorityCond); // Notify other threads that they can "compete" for a packer. (Obv. thread with higher priority)

    // Simulate packing time.
    sleep(T_PACK * order.totalPizzas);
    
    // Get time finished packing.
    clock_gettime(CLOCK_REALTIME, &timeFinishedPacking);

    // Release ovens now that packing is complete.
    acquireLock(&availableOvenLock, *oid, t);
    availableOvenCount += order.totalPizzas;
    releaseLock(&availableOvenLock, *oid, t);
    pthread_cond_signal(&availableOvenCond); // At most one thread is waiting for ovens at each point in time (FCFS Policy).

    // Release packer.
    acquireLock(&availablePackerLock, *oid, t);
    availablePackerCount += 1;
    releaseLock(&availablePackerLock, *oid, t);
    pthread_cond_signal(&availablePackerCond); // At most one thread is waiting for packer at each point in time (FCFS Policy).

    // Print message stating total time for order with <oid> to get ready. (time from customer order up to time packing was finished)
    double orderPreparationTimeMinutes = ((timeFinishedPacking.tv_sec - timeStarted.tv_sec) + (double)(timeFinishedPacking.tv_nsec - timeStarted.tv_nsec) / 1e9) / 60.0;
    acquireLock(&outputLock, *oid, t);
    printf("Order with number %d was prepared in %f minutes.\n", *oid, orderPreparationTimeMinutes);
    releaseLock(&outputLock, *oid, t);

    /* END OF PART 5 */


    /* START OF PART 6 */

    // Enforce FCFS policy.
    acquireLock(&delivererPriorityLock, *oid, t);
    while (orderPriority != delivererPriority){
        pthread_cond_wait(&delivererPriorityCond, &delivererPriorityLock);
    }

    // Acquire deliverer.
    acquireLock(&availableDelivererLock, *oid, t);
    while (availableDelivererCount == 0){
        pthread_cond_wait(&availableDelivererCond, &availableDelivererLock);
    }
    availableDelivererCount -= 1;
    releaseLock(&availableDelivererLock, *oid, t);

    // Once deliverer has been acquired.
    delivererPriority += 1;
    releaseLock(&delivererPriorityLock, *oid, t);
    pthread_cond_broadcast(&delivererPriorityCond); // Notify other threads that they can "compete" for a deliverer. (Obv. thread with higher priority)

    // Simulate time for deliverer to reach customer.
    wait = (rand_r(&seed) % (T_DEL_HIGH - T_DEL_LOW + 1)) + T_DEL_LOW;
    sleep(wait);
    
    // Get time package was delivered.
    clock_gettime(CLOCK_REALTIME, &timeDelivered);

    // Print message stating total time for order with <oid> to be delivered. (time from customer order up to delivery)
    double orderCompletionTime = ((timeDelivered.tv_sec - timeStarted.tv_sec) + (double)(timeDelivered.tv_nsec - timeStarted.tv_nsec) / 1e9) / 60.0;
    acquireLock(&outputLock, *oid, t);
    printf("Order with number %d was delivered in %f minutes.\n", *oid, orderCompletionTime);
    releaseLock(&outputLock, *oid, t);

    // Simulate time for deliverer to return.
    sleep(wait);

    // Release deliverer.
    acquireLock(&availableDelivererLock, *oid, t);
    availableDelivererCount += 1;
    releaseLock(&availableDelivererLock, *oid, t);
    pthread_cond_signal(&availableDelivererCond); // At most one thread is waiting for deliverer at each point in time (FCFS Policy).

    /* END OF PART 6*/


    /* START OF PART 7 */

    // Calculate cooling time (from time that order finished baking, up to time that order was delivered)
    double coolingTime = ((timeDelivered.tv_sec - timeFinishedBaking.tv_sec) + (double)(timeDelivered.tv_nsec - timeFinishedBaking.tv_nsec) / 1e9) / 60.0;
    
    acquireLock(&statsLock, *oid, t);

    // So that we can find max cooling time and max order completion time.
    if (orderCompletionTime > maxOrderCompletionTime) maxOrderCompletionTime = orderCompletionTime;
    if (coolingTime > maxCoolingTime) maxCoolingTime = coolingTime;

    // So that we can calculate mean order completion time and mean cooling time.
    orderCompletionTimeSum += orderCompletionTime;
    coolingTimeSum += coolingTime;

    releaseLock(&statsLock, *oid, t);

    /* END OF PART 7*/


    pthread_exit(t);
}

int main(int argc, char* argv[]){

    int numberOfCustomers;

    // Initialize locks
    initializeMutex(&outputLock);
    initializeMutex(&transactionLock);
    initializeMutex(&cookPriorityLock);
    initializeMutex(&ovenPriorityLock);
    initializeMutex(&packerPriorityLock);
    initializeMutex(&delivererPriorityLock);
    initializeMutex(&availableCookLock);
    initializeMutex(&availableOvenLock);
    initializeMutex(&availableDelivererLock);
    initializeMutex(&statsLock);

    // Initialize conds
    initializeCondition(&cookPriorityCond);
    initializeCondition(&ovenPriorityCond);
    initializeCondition(&packerPriorityCond);
    initializeCondition(&delivererPriorityCond);
    initializeCondition(&availableCookCond);
    initializeCondition(&availableOvenCond);
    initializeCondition(&availableDelivererCond);
    
    // Check if correct count of arguments is provided.
    if (argc != 3){
        printf("Error: Not enough arguments provided. (Number of Customers and initial seed is required.)\n");
        exit(-1);
    }

    numberOfCustomers = atoi(argv[1]);
    initialSeed = atoi(argv[2]);

    if (numberOfCustomers <= 0){
        printf("Error: Number of customers should be positive.\n");
        exit(-1);
    }
    
    // Allocate memory for array of threads.
    pthread_t *threads = (pthread_t *) malloc(numberOfCustomers * sizeof(pthread_t));
    if (threads == NULL){
        printf("Out of memory!");
        exit(-1);
    }
    
    // In order to use rand_r in main().
    unsigned int seed = initialSeed;

    // Create threads.
    int i;
    int wait = 0;
    int oids[numberOfCustomers];

    for (i = 0; i < numberOfCustomers; i++){
        oids[i] = i + 1;
        
        if (pthread_create(&threads[i], NULL, &simulateServiceFunc, &oids[i]) != 0) {
            printf("ERROR: Thread creation failed in main()\n");
            exit(-1);
        }

        // Sleep for random time to simulate time for new customer to connect.
        wait = (rand_r(&seed) % (T_ORDER_HIGH - T_ORDER_LOW + 1)) + T_ORDER_LOW;
        sleep(wait);
        
    }

    // Wait for threads to terminate.
    for (i = 0; i < numberOfCustomers; i++){
        if (pthread_join(threads[i], NULL) != 0){
            printf("ERROR: pthread_join() failed in main()\n");
        }
    }

    /* PRINT STATS */
    if (succesfulTransactionCount > 0){
        printf("Total specials sold: %d\n", totalSpecialSold);
        printf("Total plain sold %d\n", totalPlainSold);
        printf("Total income: %d\n", totalIncome);
        printf("Total transactions: %d\n", succesfulTransactionCount + failedTransactionCount);
        printf("Failed transaction count: %d\n", failedTransactionCount);
        printf("Mean order completion time: %f\n", orderCompletionTimeSum / succesfulTransactionCount);
        printf("Max order completion time: %f\n", maxOrderCompletionTime);
        printf("Mean cooling time: %f\n", coolingTimeSum / succesfulTransactionCount);
        printf("Max cooling time: %f\n", maxCoolingTime);
    }else{
        printf("There was no succesful transaction.\n");
    }
    

    // Destroy locks.
    destroyLock(&outputLock);
    destroyLock(&transactionLock);
    destroyLock(&cookPriorityLock);
    destroyLock(&ovenPriorityLock);
    destroyLock(&packerPriorityLock);
    destroyLock(&delivererPriorityLock);
    destroyLock(&availableCookLock);
    destroyLock(&availableOvenLock);
    destroyLock(&availableDelivererLock);
    destroyLock(&statsLock);

    // Destroy conds.
    destroyCond(&cookPriorityCond);
    destroyCond(&ovenPriorityCond);
    destroyCond(&packerPriorityCond);
    destroyCond(&delivererPriorityCond);
    destroyCond(&availableCookCond);
    destroyCond(&availableOvenCond);
    destroyCond(&availableDelivererCond);

    // Release allocated memory.
    free(threads);

    return 1;
}