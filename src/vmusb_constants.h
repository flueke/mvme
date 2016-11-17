#ifndef UUID_09e438bb_1fac_49bb_b2d5_5c514a191e48
#define UUID_09e438bb_1fac_49bb_b2d5_5c514a191e48

namespace vmusb_constants
{

static const size_t BufferMaxSize = 27 * 1024; 

namespace Buffer
{
// Header word 1
static const int LastBufferMask     = (1 << 15);
static const int IsScalerBufferMask = (1 << 14);
static const int ContinuousModeMask = (1 << 13);
static const int MultiBufferMask    = (1 << 12);
static const int NumberOfEventsMask = 0xfff;

// Optional 2nd header word
static const int NumberOfWordsMask  = 0xffff; // it's 16 bits, not 12 as the manual says

// Event header word
static const int StackIDShift       = 13;
static const int StackIDMask        = 7;
static const int ContinuationMask   = (1 << 12);
static const int EventLengthMask    = 0xfff;

/* This appears in the documentation but is not configurable and does not
 * actually appear in the output data stream. */
//static const int EventTerminator    = 0xaaaa;

static const int BufferTerminator   = 0xffff;
}

namespace GlobalMode
{
static const int Align32Mask        = (1 << 7);
static const int HeaderOptMask      = (1 << 8);
}

static const int StackIDMin = 0;
static const int StackIDMax = 7;
}

#endif
