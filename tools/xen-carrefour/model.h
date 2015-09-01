#ifndef MODEL_H
#define MODEL_H


#define COUNTER_CAP_CORE   (1ul << 0)
#define COUNTER_CAP_NB     (1ul << 1)


struct model
{
	unsigned int   node_count;
	unsigned int   core_per_node;

	unsigned long *evntsels;
	unsigned long *perfctrs;
	unsigned long *capabilities;
	unsigned long  count;
};


int get_current_model(struct model *dest);


const struct model *_current_model(void);
#define current_model   _current_model()


#endif
