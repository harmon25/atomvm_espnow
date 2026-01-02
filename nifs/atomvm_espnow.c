//
// AtomVM ESPNOW Port Driver Registration
//

#include <stdlib.h>

#include <context.h>
#include <defaultatoms.h>
#include <esp_log.h>
#include <globalcontext.h>
#include <port.h>
#include <term.h>

#include "atomvm_espnow.h"

#define TAG "espnow_port"

#include "sdkconfig.h"

//
// Port Driver Registration
//
// The port is created via: open_port({spawn, "espnow"}, Options)
// where Options = [{channel, 0..14}, {owner, self()}]
//

REGISTER_PORT_DRIVER(espnow, atomvm_espnow_init, atomvm_espnow_destroy, atomvm_espnow_create_port)
