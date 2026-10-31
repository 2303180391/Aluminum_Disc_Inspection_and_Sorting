#ifndef CANDIDATE_LOG_H
#define CANDIDATE_LOG_H
void candidate_log_init(void);
int candidate_geometry(int box[4]);
void candidate_log(float s15,float s22,unsigned k15,unsigned r15,unsigned c15,unsigned k22,unsigned r22,unsigned c22);
#endif
