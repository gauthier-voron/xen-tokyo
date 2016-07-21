#ifndef XEN_ACCOUNT_H
#define XEN_ACCOUNT_H

#include <xen/percpu.h>
#include <xen/time.h>


#define DEFINE_BIGOS_PROBE(name)                               \
	DEFINE_PER_CPU(char, __BIGOS_ ## name);                \
	DEFINE_PER_CPU(unsigned long, __BIGOS_TIME_ ## name);  \
	DEFINE_PER_CPU(unsigned long, __BIGOS_SUM_ ## name);   \
	DEFINE_PER_CPU(unsigned long, __BIGOS_COUNT_ ## name); \

#define DECLARE_BIGOS_PROBE(name)                               \
	DECLARE_PER_CPU(char, __BIGOS_ ## name);                \
	DECLARE_PER_CPU(unsigned long, __BIGOS_TIME_ ## name);  \
	DECLARE_PER_CPU(unsigned long, __BIGOS_SUM_ ## name);   \
	DECLARE_PER_CPU(unsigned long, __BIGOS_COUNT_ ## name); \


#define BIGOS_START_ACCOUNT(name)				\
	{							\
		this_cpu(__BIGOS_ ## name) = 1;			\
		this_cpu(__BIGOS_TIME_ ## name) = NOW();	\
	}
#define BIGOS_START_ACCOUNT_FOR(name, cpu)				\
	{								\
		per_cpu(__BIGOS_ ## name, cpu) = 1;			\
		per_cpu(__BIGOS_TIME_ ## name, cpu) = NOW();		\
	}
#define BIGOS_STOP_ACCOUNT(name)					\
	{								\
		this_cpu(__BIGOS_ ## name) = 0;				\
		this_cpu(__BIGOS_SUM_ ## name) +=			\
			NOW() - this_cpu(__BIGOS_TIME_ ## name);	\
		this_cpu(__BIGOS_COUNT_ ## name)++;			\
	}
#define BIGOS_STOP_ACCOUNT_FOR(name, cpu)				\
	{								\
		per_cpu(__BIGOS_ ## name, cpu) = 0;			\
		per_cpu(__BIGOS_SUM_ ## name, cpu) +=			\
			NOW() - per_cpu(__BIGOS_TIME_ ## name, cpu);	\
		per_cpu(__BIGOS_COUNT_ ## name, cpu)++;			\
	}
#define BIGOS_IS_ACCOUNTING(name)  this_cpu(__BIGOS_ ## name)

#define BIGOS_GET_THIS_SUM(name)   this_cpu(__BIGOS_SUM_ ## name)
#define BIGOS_GET_SUM(name, cpu)   per_cpu(__BIGOS_SUM_ ## name, cpu)
#define BIGOS_GET_THIS_COUNT(name) this_cpu(__BIGOS_COUNT_ ## name)
#define BIGOS_GET_COUNT(name, cpu) per_cpu(__BIGOS_COUNT_ ## name, cpu)

#define BIGOS_GET_THIS_AVG(name)					\
	(BIGOS_GET_THIS_SUM(name) / (BIGOS_GET_THIS_COUNT(name) ? : 1))
#define BIGOS_GET_AVG(name, cpu)					\
	(BIGOS_GET_SUM(name, cpu) / (BIGOS_GET_COUNT(name, cpu) ? : 1))
	

#endif
