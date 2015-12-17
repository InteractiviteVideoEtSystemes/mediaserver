/* 
 * File:   wait.h
 * Author: Sergio
 *
 * Created on 27 de mayo de 2013, 16:33
 */

#ifndef WAIT_H
#define	WAIT_H

#include "config.h"
#include "tools.h"
#include "log.h"
#include <errno.h>


class Wait
{
public:
	Wait()
	{
		//No canceled
		cancel = false;
		//Crete mutex
		pthread_mutex_init(&mutex,NULL);
		//Create condition
		pthread_cond_init(&cond,NULL);
	}

	virtual ~Wait()
	{
		//Signal just in case
		pthread_cond_signal(&cond);
		//Destroy mutex
		pthread_mutex_destroy(&mutex);
		pthread_cond_destroy(&cond);
	}

	void Signal()
	{
		//Signal
		pthread_cond_signal(&cond);
	}

	void Cancel()
	{
		//Lock
		pthread_mutex_lock(&mutex);

		//Canceled
		cancel = true;

		//Unlock
		pthread_mutex_unlock(&mutex);

		//Signal condition
		pthread_cond_signal(&cond);
	}

	bool WaitSignal(DWORD timeout)
	{
		int ret = 0;
		timespec ts;

		//Lock
		pthread_mutex_lock(&mutex);

		//if we are cancel
		if (cancel)
		{
			//Unlock
			pthread_mutex_unlock(&mutex);
			//canceled
			return false;
		}

		//Check if we have a time
		if (timeout)
		{
			//Calculate timeout
			calcTimout(&ts,timeout);

			//Wait with time out
			ret = pthread_cond_timedwait(&cond,&mutex,&ts);
			//Check if there is an errot different than timeout
			if (ret && ret!=ETIMEDOUT)
				//Print error
				Error("-Wait cond timedwait error [%d,%d]\n",ret,errno);
		} else {
			//Wait with out timout
			ret=pthread_cond_wait(&cond,&mutex);
			//Check error
			if (ret)
				//Print error
				Error("-Wait cond timedwait error [%rd,%d]\n",ret,errno);
		}

		//If we have been cancel
		if (cancel)
			//Not ok
			ret = 1;

		//Unlock
		pthread_mutex_unlock(&mutex);

		//canceled
		return !ret;
	}


private:
	bool		cancel;
	pthread_mutex_t	mutex;
	pthread_cond_t  cond;
};

#endif	/* WAIT_H */

