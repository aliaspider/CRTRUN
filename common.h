
#include <3ds.h>

#define INSTALL_URL_MAX 1024

#define R_FBI_CANCELLED MAKERESULT(RL_PERMANENT, RS_CANCELED, RM_APPLICATION, 1)
#define R_FBI_HTTP_RESPONSE_CODE MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 2)
#define R_FBI_WRONG_SYSTEM MAKERESULT(RL_PERMANENT, RS_NOTSUPPORTED, RM_APPLICATION, 3)
#define R_FBI_INVALID_ARGUMENT MAKERESULT(RL_PERMANENT, RS_INVALIDARG, RM_APPLICATION, 4)
#define R_FBI_THREAD_CREATE_FAILED MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 5)
#define R_FBI_PARSE_FAILED MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 6)
#define R_FBI_BAD_DATA MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 7)
#define R_FBI_TOO_MANY_REDIRECTS MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, 8)

#define R_FBI_NOT_IMPLEMENTED MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_APPLICATION, RD_NOT_IMPLEMENTED)
#define R_FBI_OUT_OF_MEMORY MAKERESULT(RL_FATAL, RS_OUTOFRESOURCE, RM_APPLICATION, RD_OUT_OF_MEMORY)
#define R_FBI_OUT_OF_RANGE MAKERESULT(RL_PERMANENT, RS_INVALIDARG, RM_APPLICATION, RD_OUT_OF_RANGE)

typedef struct {
    int serverSocket;
    int clientSocket;
} remoteinstall_network_data;

typedef struct ticket_info_s {
    u64 titleId;
    bool inUse;
} ticket_info;

typedef enum data_op_e {
    DATAOP_COPY,
    DATAOP_DELETE
} data_op;

typedef struct install_url_data install_url_data;
typedef struct data_op_data data_op_data;

struct data_op_data {
    install_url_data* url_data;

    u64 currProcessed;
    u64 currTotal;

    // General
    Result result;
};

struct install_url_data
{
   char url[INSTALL_URL_MAX];

   void *userData;

   u32 responseCode;
   u64 currTitleId;

   data_op_data installInfo;
};



void remoteinstall_receive_urls_network(void);

void action_install_url(const char *urls);
