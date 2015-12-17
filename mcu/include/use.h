#ifndef _USE_H_
#define _USE_H_
#include <pthread.h>
#include <errno.h>
#include "tools.h"


class Use
{
public:
	Use()
	{
		pthread_mutex_init(&mutex,NULL);
		pthread_mutex_init(&lock,NULL);
		pthread_cond_init(&cond,NULL);
		cont = 0;
	};

	~Use()
	{
		pthread_mutex_destroy(&mutex);
		pthread_mutex_destroy(&lock);
		pthread_cond_destroy(&cond);
	};

	void IncUse()
	{
		pthread_mutex_lock(&mutex);
		cont ++;
		pthread_mutex_unlock(&mutex);
	}

	void DecUse()
	{
		pthread_mutex_lock(&mutex);
		if (cont > 0) cont --;
		pthread_cond_signal(&cond);
		pthread_mutex_unlock(&mutex);
	};

	bool WaitUnusedAndLock()
	{
		pthread_mutex_lock(&lock);
		pthread_mutex_lock(&mutex);
		while(cont)
		{
			if ( pthread_cond_wait(&cond,&mutex) != 0)
			{
				// Error, signal this to the app.
                                Unlock();
				return false;
			}
		}
		return true;
	};

        /**
         * Wait during x mis
         * @param timeout timeout to wait in ms
         * @return 1 = Ok, lockled, 0 = timeout, -1 = system error
         **/
        int WaitUnusedAndLock(DWORD timeout)
	{
                int ret = 0;
            	timespec ts;

		pthread_mutex_lock(&lock);
		pthread_mutex_lock(&mutex);

		while(cont)
		{
                    if (timeout)
                    {
                            //Calculate timeout
                            calcTimout(&ts,timeout);

                            //Wait with time out
                            ret = pthread_cond_timedwait(&cond,&mutex,&ts);
                    }
                    else
                    {
                            ret = pthread_cond_wait(&cond,&mutex);
                    }


                    if ( ret )
                    {
                        Unlock();
                        return  ( ret == ETIMEDOUT ) ? 0 : -1;
                    }
		}
		return 1;
	};

	void Unlock()
	{
		pthread_mutex_unlock(&mutex);
		pthread_mutex_unlock(&lock);
	};

private:
	pthread_mutex_t	mutex;
	pthread_mutex_t	lock;
	pthread_cond_t 	cond;
	int		cont;
};

#endif
