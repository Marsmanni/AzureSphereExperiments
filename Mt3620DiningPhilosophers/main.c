#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "applibs_versions.h"
#include <applibs/log.h> 
#include "mt3620_rdb.h"

// This C application for the MT3620 Reference Development Board (Azure Sphere)
// implements the dining philosophers problem using POSIX pthread
// The algorithm is based on http://rosettacode.org/wiki/Dining_philosophers#C
//
// It uses the API for the following Azure Sphere application libraries:
// - log (messages shown in Visual Studio's Device Output window during debugging)
// 

static volatile sig_atomic_t terminationRequired = false;

typedef struct philData {
	pthread_mutex_t *forkLeft, *forkRight;
	const char *name;
	pthread_t thread;
	int gpio;
	int platesCounter;
	int isEating;
	int fail;
} Philosopher;

/// <summary>
///     Signal handler for termination requests. This handler must be async-signal-safe.
/// </summary>
static void TerminationHandler(int signalNumber)
{
    // Don't use Log_Debug here, as it is not guaranteed to be async-signal-safe.
    terminationRequired = true;
}


/// <summary>
///     Thread function for a single philosopher
/// </summary>
void *PhilPhunction(void *p) {
	Philosopher *phil = (Philosopher*)p;
	int failed;
	int triesLeft;
	int swapped = 0;
	pthread_mutex_t *forkLeft, *forkRight, *forkTemp;

	while (!terminationRequired) {
		Log_Debug("%s is thinking\n", phil->name);
		struct timespec sleepRandom = { 1 + rand() % 8, 0 };
		nanosleep(&sleepRandom, NULL);

		forkLeft = phil->forkLeft;
		forkRight = phil->forkRight;
		Log_Debug("%s is hungry\n", phil->name);
		triesLeft = 2;   /* try twice before being forceful */
		do {
			// pthread_mutex_lock and pthread_mutex_trylock:
			// see https://linux.die.net/man/3/pthread_mutex_trylock
			failed = pthread_mutex_lock(forkLeft);
			Log_Debug("%s takes %s fork\n", phil->name, swapped == 0 ? "left" : "right");
			failed = (triesLeft > 0) ? pthread_mutex_trylock(forkRight)
				: pthread_mutex_lock(forkRight);
			Log_Debug("%s takes %s fork\n", phil->name, swapped == 1 ? "left" : "right");
			if (failed) {
				Log_Debug("%s switching fork picking order\n", phil->name);
				pthread_mutex_unlock(forkLeft);
				forkTemp = forkLeft;
				forkLeft = forkRight;
				forkRight = forkTemp;
				triesLeft -= 1;
				swapped = swapped == 0 ? 1 : 0;
			}
		} while (failed && !terminationRequired);

		if (!failed) {
			phil->isEating = 1;
			phil->platesCounter++;
			GPIO_SetValue(phil->gpio, GPIO_Value_Low);
			Log_Debug("%s is eating\n", phil->name);
			sleepRandom.tv_sec = 1 + rand() % 8;
			nanosleep(&sleepRandom, NULL);
			pthread_mutex_unlock(forkRight);
			pthread_mutex_unlock(forkLeft);
			phil->isEating = 0;
			GPIO_SetValue(phil->gpio, GPIO_Value_High);
		}
	}
	return NULL;
}

/// <summary>
///     Main entry point for this sample.
/// </summary>
int main(int argc, char *argv[])
{
    Log_Debug("Dining philosophers starting.\n");

    // Register a SIGTERM handler for termination requests
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = TerminationHandler;
    sigaction(SIGTERM, &action, NULL);

	// Initialize ouput LEDs
	int gpios[5];
	gpios[0] = GPIO_OpenAsOutput(MT3620_RDB_LED1_GREEN, GPIO_OutputMode_PushPull, GPIO_Value_High);
	gpios[1] = GPIO_OpenAsOutput(MT3620_RDB_LED2_GREEN, GPIO_OutputMode_PushPull, GPIO_Value_High);
	gpios[2] = GPIO_OpenAsOutput(MT3620_RDB_LED3_GREEN, GPIO_OutputMode_PushPull, GPIO_Value_High);
	gpios[3] = GPIO_OpenAsOutput(MT3620_RDB_LED4_GREEN, GPIO_OutputMode_PushPull, GPIO_Value_High);
	gpios[4] = GPIO_OpenAsOutput(MT3620_RDB_STATUS_LED_GREEN, GPIO_OutputMode_PushPull, GPIO_Value_High);

	// Initializing philosopher threads
	const char *nameList[] = { "Socrates", "Plato", "Phytagoras", "Aristotle", "Diogenes" };
	pthread_mutex_t forks[5];
	Philosopher philosophers[5];
	Philosopher *phil;
	int i;
	int failed;

	for (i = 0; i < 5; i++) {
		failed = pthread_mutex_init(&forks[i], NULL);
		if (failed) {
			Log_Debug("Failed to initialize mutexes.\n");
			exit(1);
		}
	}

	for (i = 0; i < 5; i++) {
		phil = &philosophers[i];
		phil->name = nameList[i];
		phil->forkLeft = &forks[i];
		phil->forkRight = &forks[(i + 1) % 5];
		phil->platesCounter = 0;
		phil->isEating = 0;
		phil->fail = pthread_create(&phil->thread, NULL, PhilPhunction, phil);;
		phil->gpio = gpios[i];
		if (phil->fail) {
			Log_Debug("Failed to create thread for %s.\n", phil->name);
			exit(1);
		}
	}

	// Main loop
    const struct timespec sleepTime = {1, 0};
    while (!terminationRequired) {
		Log_Debug("Hello dining philosophers (");
		for (i = 0; i < 5; i++) {
			Log_Debug(philosophers[i].isEating == 0 ? "0" : "1");
		}
		Log_Debug(" - (");
		for (i = 0; i < 5; i++) {
			Log_Debug("%i ", philosophers[i].platesCounter);
		}
		Log_Debug(")\n");
        nanosleep(&sleepTime, NULL);
    }

    Log_Debug("Dining philosophers exiting.\n");
    return 0;
}

