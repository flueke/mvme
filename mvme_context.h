#ifndef UUID_9196420f_dd04_4572_8e4b_952039634913
#define UUID_9196420f_dd04_4572_8e4b_952039634913

class VMEController;
class VMEModule;
class MesytecChain;
class VMUSB_Stack;

struct MVMEContext
{
    VMEController *controller = 0;
    QVector<VMEModule *> modules;
    QVector<MesytecChain *> mesytec_chains;
    QVector<VMUSB_Stack *> vmusb_stacks;
};

#endif
