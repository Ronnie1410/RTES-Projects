/*
--------------------------------------------------
CSE-522 Real Time Embedded Systems Assignment 4
Author: Roshan Raj K
ASUID:- 1222478062
*/
#include <logging/log.h>
LOG_MODULE_REGISTER(polling_server_module, LOG_LEVEL_DBG);
#include <zephyr.h>
#include <sys/printk.h>
#include <shell/shell.h>
#include <stdlib.h>
#include "task_model_p4_new.h"

#define BACKGROUND_PRIORITY     14

//Initialize Task Body Flag to False
static bool task_body_flag = false, poll_server_run = false;
static uint64_t total_response_time = 0U;
static int total_requests_served = 0;


//Define thread stack memory region for all the threads
K_THREAD_STACK_ARRAY_DEFINE(thread_stack_area, NUM_THREADS+1, STACK_SIZE);

static struct k_thread thread_ctl_block[NUM_THREADS+1];
int count_in, count_out;

//Semaphores for communication between each thread and it's corresponding Timer Callback Function
struct k_sem thread_sem[NUM_THREADS];//, shell_sem;
struct k_sem poll_sem;

//Function to handle shell command
// static int shell_command(const struct shell *shell, size_t argc, char **argv)
// {
//     ARG_UNUSED(argc);
//     ARG_UNUSED(argv);
//     k_sem_give(&shell_sem);
    
//     return 0;
// }

// //Register the shell command function
// SHELL_CMD_ARG_REGISTER(activate, NULL, "Activates all the Threads", shell_command, 1, 0);

static void priority_high_handler(struct k_work *item)
{
    k_thread_priority_set(poll_info.poll_tid, poll_info.priority);
}

//Define work item for high priority setting
K_WORK_DEFINE(priority_high_work, priority_high_handler);

static void priority_low_handler(struct k_work *item)
{
    k_thread_priority_set(poll_info.poll_tid, BACKGROUND_PRIORITY);
}

//Define work item for low priority setting
K_WORK_DEFINE(priority_low_work, priority_low_handler);

//Polling server replenishment timer
void poll_expiry_function(struct k_timer *timer_info)
{
    poll_info.left_budget = 1000000*BUDGET;
    k_work_submit(&priority_high_work);
}

K_TIMER_DEFINE(polling_timer, poll_expiry_function, NULL);


void budget_expiry_function(struct k_timer *timer_info)
{
    poll_info.left_budget = 0;
    k_work_submit(&priority_low_work);
}

K_TIMER_DEFINE(budget_timer, budget_expiry_function, NULL);


void aperiodic_switched_in()
{   
    //LOG_DBG("Switched In");
    if(k_current_get() == poll_info.poll_tid)
    {   
        if(poll_info.left_budget>0)
        {
            k_timer_start(&budget_timer, K_NSEC(poll_info.left_budget), K_NSEC(0));
            
        }
    }
}

//Aperiod Switched Out routine
void aperiodic_switched_out()
{   
    //LOG_DBG("Switched Out");
    if(k_current_get() == poll_info.poll_tid)
    {   
        
        //poll_out_cycles = k_cycle_get_32();
        if(k_thread_priority_get(poll_info.poll_tid) == POLL_PRIO)
        {
            //poll_info.last_switched_in = k_cycle_get_32();
            //k_cyc_to_ns_floor32(sub32(poll_info.last_switched_in, k_cycle_get_32()));
            poll_info.left_budget = 1000000*k_timer_remaining_get(&budget_timer); 
            k_timer_stop(&budget_timer);
        }
    }
}

/*Timer Callback function
    Expires periodically and gives semaphore for continuation of task body
*/
void timer_expiry_callback(struct k_timer *timer_info)
{
    int thread_number = *((int*)timer_info->user_data);
    k_sem_give(&thread_sem[thread_number]);
}

//Periodic routine: Creates threads as defined by NUM_THREADS and struct task_s in task_model.h
void periodic_routine(void *arg1, void *arg2, void *arg3)
{   
    //Initialize each thread's information
    struct task_s thread_data = *((struct task_s*)arg1);
    int t_id = *(int *) arg2;
    ARG_UNUSED(arg3);

    LOG_DBG("In thread %d\n", t_id);
    // Define Timer thread
    struct k_timer thread_timer;
    //Initialize Timer
    k_timer_init(&thread_timer, timer_expiry_callback, NULL);

    //Set timer user data for each thread's Timer Callback
    thread_timer.user_data = &t_id;

    //Wait for activation from main
    k_sem_take(&thread_sem[t_id], K_FOREVER);
    //Start periodic timer
    k_timer_start(&thread_timer, K_MSEC(thread_data.period), K_MSEC(thread_data.period));
    while(task_body_flag)
    {
        //task body computation
        looping(thread_data.loop_iter);
        //Check and print if the task has missed deadline
        if((k_timer_status_get(&thread_timer))>0)
        {
            //LOG_DBG("%s missed the deadline\n",thread_data.t_name);
        }
        //Wait for semaphore till the end of period
        k_sem_take(&thread_sem[t_id], K_FOREVER);  

    }
    k_timer_stop(&thread_timer);
}

//Polling Server Routine
void polling_server_routine(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg3);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg1);
    struct req_type data;
    LOG_DBG("In polling thread\n");
    k_sem_take(&poll_sem, K_FOREVER);
    
    k_timer_start(&polling_timer, K_MSEC(poll_info.period), K_MSEC(poll_info.period));
    while(poll_server_run)
    {

        if(!k_msgq_get(&req_msgq,&data,K_NO_WAIT))
        {   
            if(poll_info.left_budget>0)
            { 
                k_timer_start(&budget_timer, K_NSEC(poll_info.left_budget), K_NSEC(0));
                looping(data.iterations);
                total_response_time += sub32(data.arr_time,k_cycle_get_32()); 
                total_requests_served ++;
                poll_info.left_budget = 1000000*k_timer_remaining_get(&budget_timer);
            }
            else{
                looping(data.iterations);
                total_response_time += sub32(data.arr_time,k_cycle_get_32()); 
                total_requests_served ++;
            }           
                        
        }
        else {
            poll_info.left_budget = 0U;
            k_timer_stop(&budget_timer);
            k_thread_priority_set(poll_info.poll_tid, BACKGROUND_PRIORITY);
        }
    }
    k_timer_stop(&polling_timer);
    k_timer_stop(&budget_timer);
}

//Main Program
void main(void)
{
    int i;//local variable for loops

    //Define timer user data for each thread
    int timer_compute_data[NUM_THREADS];

    //Define variables to store thread IDs
    k_tid_t  thread_id[NUM_THREADS];

    //Initialize semaphore for shell command
    //k_sem_init(&shell_sem,0,1);
    k_sem_init(&poll_sem,0,1);

    //Initialize semaphore and timer user data for each thread
    for(i =0; i<NUM_THREADS;i++)
    {
        k_sem_init(&thread_sem[i], 0, 1);
        timer_compute_data[i] = i;
    }


    //Start timer the timer for aperiodic request
    k_timer_start(&req_timer, K_USEC(ARR_TIME), K_NO_WAIT); 
    //Create periodic tasks and set task name
    for(i=0; i<NUM_THREADS; i++)
    {
        thread_id[i] = k_thread_create(&thread_ctl_block[i], thread_stack_area[i], STACK_SIZE, 
                        periodic_routine, &threads[i], &timer_compute_data[i], NULL, threads[i].priority, 0, K_MSEC(5));

        k_thread_name_set(thread_id[i], threads[i].t_name);
    }
    //Task Body Flag set true 
    task_body_flag = true;
    
    //Create and start polling thread
    poll_info.poll_tid = k_thread_create(&thread_ctl_block[NUM_THREADS], thread_stack_area[NUM_THREADS], STACK_SIZE, 
                       polling_server_routine, NULL,NULL, NULL, poll_info.priority, 0, K_MSEC(5));
    
    k_thread_name_set(poll_info.poll_tid, poll_info.t_name);

    poll_server_run =  true;

    //Activate computation of task body of all tasks
    for(i=0; i<NUM_THREADS; i++)
    {
        k_sem_give(&thread_sem[i]);
    }

    k_sem_give(&poll_sem);
    //Main waits till total execution time is elapsed
    k_msleep(TOTAL_TIME);
    LOG_DBG("Main woke up");
    k_timer_stop(&req_timer);
    //Task Body Flag set to false to terminate all threads
    task_body_flag = false;
    poll_server_run =  false;

    //Print the information
    printk("Total aperiodic requests served by polling Server: %d\n", total_requests_served);
    printk("Total response time: %lld ms\n", k_cyc_to_ms_floor64(total_response_time));
    printk("Average response time: %lld ms\n", k_cyc_to_ms_floor64(total_response_time)/(uint64_t)total_requests_served);

    //Wake up sleeping threads
    for(i=0; i<NUM_THREADS; i++)
    {
        k_sem_give(&thread_sem[i]);
    }

    //Main waits for running threads to complete and join
    for(i=0; i<NUM_THREADS+1; i++)
    {
        k_thread_join(&thread_ctl_block[i], K_FOREVER);
    }
    //Print on console to show the program has executed completely
    printk("Exit from Main\n");
}