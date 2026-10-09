#ifndef __cseqp__
#define __cseqp__


void	__CSPPostNextSeqEvent(ALCSPlayer *seqp);

#ifdef GEVR
/* Timeout recovery on the port's single audio/game thread. */
void gevrCSPForceStop(ALCSPlayer *seqp);
#endif


#endif /* __cseqp__ */
