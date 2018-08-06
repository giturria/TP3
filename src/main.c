/*
 * @brief FreeRTOS examples
 *
 * @note
 * Copyright(C) NXP Semiconductors, 2014
 * All rights reserved.
 *
 * @par
 * Software that is described herein is for illustrative purposes only
 * which provides customers with programming information regarding the
 * LPC products.  This software is supplied "AS IS" without any warranties of
 * any kind, and NXP Semiconductors and its licensor disclaim any and
 * all warranties, express or implied, including all implied warranties of
 * merchantability, fitness for a particular purpose and non-infringement of
 * intellectual property rights.  NXP Semiconductors assumes no responsibility
 * or liability for the use of the software, conveys no license or rights under any
 * patent, copyright, mask work right, or any other intellectual property rights in
 * or to any products. NXP Semiconductors reserves the right to make changes
 * in the software without notification. NXP Semiconductors also makes no
 * representation or warranty that such application will be suitable for the
 * specified use without further testing or modification.
 *
 * @par
 * Permission to use, copy, modify, and distribute this software and its
 * documentation is hereby granted, under NXP Semiconductors' and its
 * licensor's relevant copyrights in the software, without fee, provided that it
 * is used in conjunction with NXP Semiconductors microcontrollers.  This
 * copyright, permission, and disclaimer notice must appear in all copies of
 * this code.
 */

#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdlib.h>

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/
#define TP3_4 (1)		/* T1 periodica 500ms + IRQ (productor) -> semaforo ->
 	 	 	 	 	 	 	 	   T2 (consumidor/productor) -> Cola -> T3 (consumidor)*/
#define TP3_5 (2)		/* Idem APP1 pero con direccion y roles invertidos */
#define TP3_6 (3)		/* Idem APP1 pero 3 tareas comparten el uso de un LED
								   por un periodo mayor al timeslice y no pueden mezclarse */

								   
#define TP3_X (TP3_4)

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/

/*****************************************************************************
 * Private functions
 ****************************************************************************/

/* Sets up system hardware */
static void prvSetupHardware(void)
{
	SystemCoreClockUpdate();
	Board_Init();

	/* Initial LED state is off */
	Board_LED_Set(LED, LED_OFF);
}

/* The interrupt number to use for the software interrupt generation.  This
 * could be any unused number.  In this case the first chip level (non system)
 * interrupt is used, which happens to be the watchdog on the LPC1768.  WDT_IRQHandler */
/* interrupt is used, which happens to be the DAC on the LPC4337 M4.  DAC_IRQHandler */
#define mainSW_INTERRUPT_ID		(0)
/* The priority of the software interrupt.  The interrupt service routine uses
 * an (interrupt safe) FreeRTOS API function, so the priority of the interrupt must
 * be equal to or lower than the priority set by
 * configMAX_SYSCALL_INTERRUPT_PRIORITY - remembering that on the Cortex-M3 high
 * numeric values represent low priority values, which can be confusing as it is
 * counter intuitive. */
#define mainSOFTWARE_INTERRUPT_PRIORITY	(5)

/* Configura las interrupciones */
static void prvSetupSoftwareInterrupt()
{
	/* The interrupt service routine uses an (interrupt safe) FreeRTOS API
	 * function so the interrupt priority must be at or below the priority defined
	 * by configSYSCALL_INTERRUPT_PRIORITY. */
	NVIC_SetPriority(mainSW_INTERRUPT_ID, mainSOFTWARE_INTERRUPT_PRIORITY);

	/* Enable the interrupt. */
	NVIC_EnableIRQ(mainSW_INTERRUPT_ID);
}


#if (TP3_X == TP3_4)
/* T1 periodica 500ms + IRQ (productor) -> semaforo ->
   T2 (consumidor/productor) -> Cola -> T3 (consumidor)*/

const char *TextForTask1="Tarea 1 periodica ejecutandose. Activando interrupcion\r\n";
const char *TextForIRQ="Otorgando semaforo en interrupcion.\r\n";
const char *TextForTask2="Semaforo recibido. Enviando datos a la cola.\r\n";
const char *QueueMessage="Mensaje transmitido por cola.\r\n";
const char *TextForTask3="Tarea 3 receptora por cola ejecutandose. Mensaje:\r\n";
/* Se crea el handler del semaforo*/
xSemaphoreHandle xTask2Semaphore;
/* Se crea el handler de la cola */
xQueueHandle xQueue;

/* Tarea periodica de 500 ms */
void PeriodicTask(void *pvParameters){
	portTickType xLastExecutionTime;
	while (1)
	{
		vTaskDelayUntil(&xLastExecutionTime, 500 / portTICK_RATE_MS);	// 500 ms de delay
		DEBUGOUT(TextForTask1);
		NVIC_SetPendingIRQ(mainSW_INTERRUPT_ID);
	}
}

/*Handler de la interrupcion productora */
void DAC_IRQHandler(void){
	portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
	DEBUGOUT(TextForIRQ);
	/* 'Give' the semaphore to unblock the task. */
    xSemaphoreGiveFromISR(xTask2Semaphore, &xHigherPriorityTaskWoken);

    /* Clear the software interrupt bit using the interrupt controllers
	 * Clear Pending register. */
    NVIC_ClearPendingIRQ(mainSW_INTERRUPT_ID);

	/* Giving the semaphore may have unblocked a task - if it did and the
	 * unblocked task has a priority equal to or above the currently executing
     * task then xHigherPriorityTaskWoken will have been set to pdTRUE and
	 * portEND_SWITCHING_ISR() will force a context switch to the newly unblocked
	 * higher priority task.
	 *
	 * NOTE: The syntax for forcing a context switch within an ISR varies between
	 * FreeRTOS ports.  The portEND_SWITCHING_ISR() macro is provided as part of
	 * the Cortex-M3 port layer for this purpose.  taskYIELD() must never be called
	 * from an ISR! */
	portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}

/* Tarea 2 consumidora del semaforo, productora de cola */
void InterruptHandlerTask(void *pvParameters){
	portBASE_TYPE xStatus;
	xSemaphoreTake(xTask2Semaphore, (portTickType) 0);
	while(1)
	{
		xSemaphoreTake(xTask2Semaphore,portMAX_DELAY);
		DEBUGOUT(TextForTask2);
		xStatus = xQueueSendToBack(xQueue, &QueueMessage,(portTickType)0);

		if (xStatus != pdPASS) {
			/* We could not write to the queue because it was full - this must
			 * be an error as the receiving task should make space in the queue
			 * as soon as both sending tasks are in the Blocked state. */
			DEBUGOUT("No se pudo enviar a la cola.\r\n");
		}
	}
}

/* Tarea 3 consumidora de cola*/
void QueueReceiverTask(void *pvParameters){
	const portTickType xTicksToWait = 600 / portTICK_RATE_MS;	// tiene que ser un tiempo
																// mayor al de la interrupcion
	char *ReceivedText=NULL;
	portBASE_TYPE xStatus;
	while(1)
	{
		/* As this task unblocks immediately that data is written to the queue this
		 * call should always find the queue empty. */
		if (uxQueueMessagesWaiting(xQueue) != 0) {
			DEBUGOUT("La cola deberia estar vacia!\r\n");
		}

		/* The first parameter is the queue from which data is to be received.  The
		 * queue is created before the scheduler is started, and therefore before this
		 * task runs for the first time.
		 *
		 * The second parameter is the buffer into which the received data will be
		 * placed.  In this case the buffer is simply the address of a variable that
		 * has the required size to hold the received data.
		 *
		 * The last parameter is the block time � the maximum amount of time that the
		 * task should remain in the Blocked state to wait for data to be available should
		 * the queue already be empty. */
		xStatus = xQueueReceive(xQueue, &ReceivedText, xTicksToWait);

		if (xStatus == pdPASS) {
		/* Data was successfully received from the queue, print out the received
		 * value. */
			DEBUGOUT(TextForTask3);
			DEBUGOUT(ReceivedText);
		}
		else {
			/* We did not receive anything from the queue even after waiting for 100ms.
			 * This must be an error as the sending tasks are free running and will be
			 * continuously writing to the queue. */
			DEBUGOUT("No se pudo recibir de la cola ejecutandose.\r\n");
		}
	}
}

int main(void){
	/* Sets up system hardware */
	prvSetupHardware();

	/* Se crea el semaforo */
	vSemaphoreCreateBinary(xTask2Semaphore);
	/* Se crea la cola */
	xQueue = xQueueCreate(5, sizeof(char *));

	/* Se comprueba que el semaforo y la cola se hayan creado con éxito */
	if (xTask2Semaphore != (xSemaphoreHandle) NULL && xQueue != (xQueueHandle)NULL) {
		DEBUGOUT("\r\n***** Ej 4: *****\n\r");
		/* Enable the software interrupt and set its priority. */
    	prvSetupSoftwareInterrupt();
		xTaskCreate(QueueReceiverTask,(char*)"Tarea receptora de cola",configMINIMAL_STACK_SIZE,
				NULL,(tskIDLE_PRIORITY + 3UL),(xTaskHandle *)NULL);
		xTaskCreate(InterruptHandlerTask,(char*)"Tarea asociada a interrupcion",
				configMINIMAL_STACK_SIZE,NULL,(tskIDLE_PRIORITY + 2UL),(xTaskHandle *)NULL);
		xTaskCreate(PeriodicTask,(char*)"Tarea periodica",configMINIMAL_STACK_SIZE,NULL,
		   		(tskIDLE_PRIORITY + 1UL), (xTaskHandle *) NULL);


		vTaskStartScheduler();
	}
	while(1);
	return ((int) NULL);

}

#endif

#if (TP3_X == TP3_5)
/* Idem punto4 pero con direccion y roles invertidos */

const char *TextForTask1="Tarea 1. Activando interrupcion\r\n";
const char *TextForTask2="Tarea 2. Semaforo recibido. Enviando a cola.\r\n";
const char *TextForTask3="Tarea 3. Enviando Semaforo\r\n";
const char *TextForIRQ="Interrupcion. Eviando de cola\r\n";
const char *QueueMessage="Un mensaje interesante\r\n";

char *DAC_IRQptr;
/* Se crea el handler de la task que da el semaforo */
TaskHandle_t xTask3Handle;
/* Se crea el handler del semaforo*/
xSemaphoreHandle xTask2Semaphore;
/* Se crea el handler de la cola */
xQueueHandle xQueue;

/* Se mantiene la tarea periodica del punto 4. Pero ahora la interrupcion hace que se
 * consuma la data */
/* Tarea periodica de 500 ms */
void PeriodicTask(void *pvParameters){
	portTickType xLastExecutionTime1;
	xLastExecutionTime1 = xTaskGetTickCount();
	while (1)
	{
		vTaskDelayUntil(&xLastExecutionTime1, 500 / portTICK_RATE_MS);	// 500 ms de delay
		DEBUGOUT(TextForTask1);
		NVIC_SetPendingIRQ(mainSW_INTERRUPT_ID);
	}
}

/* Handler de la interrupcion consumidora
 * Recibe la cola llena */
void DAC_IRQHandler(void){
	portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
	DEBUGOUT(TextForIRQ);
	/* Con este if chequeo por si la cola todavia no se escribio */
	if (xQueueReceiveFromISR(xQueue, &DAC_IRQptr, &xHigherPriorityTaskWoken) != errQUEUE_EMPTY)
	{
		DEBUGOUT(DAC_IRQptr);
	}
    /* Clear the software interrupt bit using the interrupt controllers
     * Clear Pending register. */
	NVIC_ClearPendingIRQ(mainSW_INTERRUPT_ID);
    /* "pasar el mate" desde la interrupcion */
	portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}

/* Tarea 2
 * Consumidora de semaforo, Generadora de cola  */
void InterruptSenderTask(void *pvParameters){
	portBASE_TYPE xStatus;
	const portTickType xTicksToWait=600/portTICK_RATE_MS; //lo dejo por las dudas
	xSemaphoreTake(xTask2Semaphore, (portTickType) 0);
	while(1)
	{
		xSemaphoreTake(xTask2Semaphore,portMAX_DELAY);
		xStatus = xQueueSendToBack(xQueue, &QueueMessage, xTicksToWait);

		DEBUGOUT(TextForTask2);
		if (xStatus != pdPASS) {
			/* We could not write to the queue because it was full - this must
			 * be an error as the receiving task should make space in the queue
			 * as soon as both sending tasks are in the Blocked state. */
			DEBUGOUT("No se pudo enviar a la cola.\r\n");
		}
	}
}
/* Tarea 3
 * Generadora de semaforo*/
void SemaphoreTask (void *pvParameters){
	portTickType xLastExecutionTime2;
	xLastExecutionTime2 = xTaskGetTickCount();
	while(1)
	{
		//vTaskDelayUntil(&xLastExecutionTime2, 500 / portTICK_RATE_MS);	// 500 ms de delay
		DEBUGOUT(TextForTask3);
		 if( xSemaphoreGive( xTask2Semaphore ) != pdTRUE )
		 {
		     // We would expect this call to fail because we cannot give
			 // a semaphore without first "taking" it!
			 DEBUGOUT("NO PUDE DAR SEMAFORO\r\n");
			 vTaskDelayUntil(&xLastExecutionTime2, 700 / portTICK_RATE_MS);
		 }
	}
}

int main(void){
	/* Sets up system hardware */
	prvSetupHardware();

	/* Se crea el semaforo */
	vSemaphoreCreateBinary(xTask2Semaphore);
	/* Se crea la cola */
	xQueue = xQueueCreate(5, sizeof(char *));

	/* Se comprueba que el semaforo y la cola se hayan creado con éxito */
	if (xTask2Semaphore != (xSemaphoreHandle) NULL && xQueue != (xQueueHandle)NULL) {
		DEBUGOUT("\r\n***** Ej 5: *****\n\r");
		/* Enable the software interrupt and set its priority. */
    	prvSetupSoftwareInterrupt();
    	xTaskCreate(InterruptSenderTask,(char*)"Tarea asociada a interrupcion",
    					configMINIMAL_STACK_SIZE,NULL,(tskIDLE_PRIORITY + 3UL),(xTaskHandle *)NULL);
		xTaskCreate(SemaphoreTask,(char*)"Tarea semaforo",configMINIMAL_STACK_SIZE,
						NULL,(tskIDLE_PRIORITY + 1UL),&xTask3Handle);
		xTaskCreate(PeriodicTask,(char*)"Tarea periodica",configMINIMAL_STACK_SIZE,NULL,
		   		(tskIDLE_PRIORITY + 2UL), (xTaskHandle *) NULL);


		vTaskStartScheduler();
	}
	while(1);
	return ((int) NULL);

}

#endif


#if (TP3_X == TP3_6)
/* Idem punto4 pero 3 tareas comparten el uso de un LED
   por un periodo mayor al timeslice y no pueden mezclarse */

   
// mutex
xSemaphoreHandle xMutex;

static void vTaskLed(void *pvParameters)
{
	TickType_t xLastWakeTime;

	bool *sequence = (bool *)pvParameters;

	while (1) {

		size_t i = 0;

		xSemaphoreTake(xMutex, portMAX_DELAY);
		vPrintString("empieza secuencia de tarea ");
		vPrintString(pcTaskGetTaskName(xTaskGetCurrentTaskHandle()));
		vPrintString("\r\n");

		for (i = 0; i < 4; ++i) {
			xLastWakeTime = xTaskGetTickCount();
			if(sequence[i]){
				//si la secuencia tiene un uno prendo el primer led y apago el segundo
				gpioWrite( LED1, ON );
				gpioWrite( LED2, OFF);
			}else{
				//si la secuencia tiene un uno prendo el segundo led y apago el primero
				gpioWrite( LED2, ON );
				gpioWrite( LED3, OFF);
			}
			//delay para poder ver la secuencia
			vTaskDelayUntil( &xLastWakeTime, PERIODIC_TASK_DELAY );
		}
		//terina y devuelve semaforo
		xSemaphoreGive(xMutex);
		//delay
		vTaskDelay(PERIODIC_TASK_DELAY*10);
	}
}

int main(void)
{
	//inicializo hardware
	prvSetupHardware();

	//mutex
   	xMutex = xSemaphoreCreateMutex();

   	//defino las secuencias de cada tarea
   	bool sequenceTask1[] = {true,true,true,true};
   	bool sequenceTask2[] = {false,false,false,false};
   	bool sequenceTask3[] = {false,true,false,true};

   	//creo las tareas
    xTaskCreate(vTaskLed, (char *) "vTaskLed1", configMINIMAL_STACK_SIZE, sequenceTask1,
           				(tskIDLE_PRIORITY + 1UL), (xTaskHandle *) NULL);

    xTaskCreate(vTaskLed, (char *) "vTaskLed2", configMINIMAL_STACK_SIZE, sequenceTask2,
               			(tskIDLE_PRIORITY + 1UL), (xTaskHandle *) NULL);

    xTaskCreate(vTaskLed, (char *) "vTaskLed3", configMINIMAL_STACK_SIZE, sequenceTask3,
						(tskIDLE_PRIORITY + 1UL), (xTaskHandle *) NULL);

    //inicio el scheduler
	vTaskStartScheduler();

	while (1);

	return ((int) NULL);
}

   
   
   
#endif

