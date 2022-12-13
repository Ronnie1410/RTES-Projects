/*
--------------------------------------------------
CSE-522 Real Time Embedded Systems Assignment 1
Author: Roshan Raj K
ASUID:- 1222478062
*/

#include <zephyr.h>
#include <sys/printk.h>
#include <shell/shell.h>
#include "task_model.h"

//Macro defining stack size for each thread
#define STACK_SIZE   512

// Initialize Task Body Flag to False
static bool task_body_flag = false;

//Define thread stack memory region for all the threads
K_THREAD_STACK_ARRAY_DEFINE(thread_stack_area, NUM_THREADS, STACK_SIZE);

//Define variables for thread control block data for each thread
static struct k_thread thread_ctl_block[NUM_THREADS];

//Mutexes created for each periodic thread for local computation <compute_2>
struct k_mutex mutex[NUM_MUTEXES];

//Semaphores for communication between each thread and it's corresponding Timer Callback Function
struct k_sem thread_sem[NUM_THREADS], shell_sem;

//Function to handle shell command
static int shell_command(const struct shell *shell, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    k_sem_give(&shell_sem);
    
    return 0;
}

//Register the shell command function
SHELL_CMD_ARG_REGISTER(activate, NULL, "Activates all the Threads", shell_command, 1, 0);

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

    // Define Timer thread
    struct k_timer thread_timer;
    //Initialize Timer
    k_timer_init(&thread_timer, timer_expiry_callback, NULL);

    //Set timer user data for each thread's Timer Callback
    thread_timer.user_data = &t_id;
       
    //Variable for local computation block
    volatile uint64_t n;

    //Wait for activation from main
    k_sem_take(&thread_sem[t_id], K_FOREVER);
    //Start periodic timer
    k_timer_start(&thread_timer, K_MSEC(thread_data.period), K_MSEC(thread_data.period));
    while(task_body_flag)
    {
        n = thread_data.loop_iter[0];
        //<compute_1>
        while(n > 0)    n--;
        n = thread_data.loop_iter[1];
        k_mutex_lock(&mutex[thread_data.mutex_m], K_FOREVER); 
        //<compute_2>   
        while(n > 0)    n--;
        k_mutex_unlock(&mutex[thread_data.mutex_m]);
        n = thread_data.loop_iter[2];
        //<compute_3>  
        while(n > 0)    n--;
        //Check and print if the task has missed deadline
        if((k_timer_status_get(&thread_timer))>0)
        {
            printk("%s missed the deadline\n",thread_data.t_name);
        }
        //Wait for semaphore till the end of period
        k_sem_take(&thread_sem[t_id], K_FOREVER);  

    }
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
    k_sem_init(&shell_sem,0,1);

    //Initialize and mutexes used by each thread
    for(i=0; i<NUM_MUTEXES;i++)
    {
        k_mutex_init(&mutex[i]);
    }
    
    //Initialize semaphore and timer user data for each thread
    for(i =0; i<NUM_THREADS;i++)
    {
        k_sem_init(&thread_sem[i], 0, 1);
        timer_compute_data[i] = i;
    }
        
    //Create periodic tasks and set task name
    for(i=0; i<NUM_THREADS; i++)
    {
        thread_id[i] = k_thread_create(&thread_ctl_block[i], thread_stack_area[i], STACK_SIZE, 
                        periodic_routine, &threads[i], &timer_compute_data[i], NULL, threads[i].priority, 0, K_NO_WAIT);

        k_thread_name_set(thread_id[i], threads[i].t_name);
    }
    //Task Body Flag set true 
    task_body_flag = true;

    //wait for activate command from shell
    k_sem_take(&shell_sem, K_FOREVER); 

    //Activate computation of task body of all tasks
    for(i=0; i<NUM_THREADS; i++)
    {
        k_sem_give(&thread_sem[i]);
    }

    //Main waits till total execution time is elapsed
    k_msleep(TOTAL_TIME);

    //Task Body Flag set to false to terminate all threads
    task_body_flag = false;

    //Main waits for running threads to complete and join
    for(i=0; i<NUM_THREADS; i++)
    {
        k_thread_join(&thread_ctl_block[i], K_FOREVER);
    }
    //Print on console to show the program has executed completely
    printk("Exit from Main\n");
}