/*******************************************************************************
*   Ledger App - Bitcoin Wallet
*   (c) 2016-2019 Ledger
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
********************************************************************************/

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include "os.h"

#include "btchip_internal.h"

#include "os_io_seproxyhal.h"

#include "btchip_apdu_constants.h"
#include "btchip_display_variables.h"

#define BTCHIP_TECHNICAL_NOT_IMPLEMENTED 0x99

#define COMMON_CLA               0xB0
#define COMMON_INS_GET_WALLET_ID 0x04

#ifndef HAVE_WALLET_ID_SDK

unsigned int const U_os_perso_seed_cookie[] = {
  0xda7aba5e,
  0xc1a551c5,
};

void handleGetWalletId(volatile unsigned short *tx) {
  unsigned char t[64];
  cx_ecfp_256_private_key_t priv;
  cx_ecfp_256_public_key_t pub;
  // seed => priv key
  os_perso_derive_node_bip32(CX_CURVE_256K1, U_os_perso_seed_cookie, 2, t, NULL);
  // priv key => pubkey
  cx_ecdsa_init_private_key(CX_CURVE_256K1, t, 32, &priv);
  cx_ecfp_generate_pair(CX_CURVE_256K1, &pub, &priv, 1);
  // pubkey -> sha512
  cx_hash_sha512(pub.W, sizeof(pub.W), t, sizeof(t));
  // ! cookie !
  os_memmove(G_io_apdu_buffer, t, 64);
  btchip_context_D.sw = 0x9000;
  *tx = 64;
}

#endif // HAVE_WALLET_ID_SDK

void app_dispatch(void) {
    unsigned char cla;
    unsigned char ins;
    unsigned char dispatched;

    // nothing to reply for now
    btchip_context_D.outLength = 0;
    btchip_context_D.io_flags = 0;

    BEGIN_TRY {
        TRY {

#ifndef HAVE_WALLET_ID_SDK

      if ((G_io_apdu_buffer[ISO_OFFSET_CLA] == COMMON_CLA) && (G_io_apdu_buffer[ISO_OFFSET_INS] == COMMON_INS_GET_WALLET_ID)) {
        handleGetWalletId(&btchip_context_D.outLength);
        goto sendSW;
      }

#endif

            // If halted, then notify
            SB_CHECK(btchip_context_D.halted);
            if (SB_GET(btchip_context_D.halted)) {
                btchip_context_D.sw = BTCHIP_SW_HALTED;
                goto sendSW;
            }

            cla = G_io_apdu_buffer[ISO_OFFSET_CLA];
            ins = G_io_apdu_buffer[ISO_OFFSET_INS];
            for (dispatched = 0; dispatched < DISPATCHER_APDUS; dispatched++) {
                if ((cla == DISPATCHER_CLA[dispatched]) &&
                    (ins == DISPATCHER_INS[dispatched])) {
                    break;
                }
            }
            if (dispatched == DISPATCHER_APDUS) {
                btchip_context_D.sw = BTCHIP_SW_INS_NOT_SUPPORTED;
                goto sendSW;
            }
            if (DISPATCHER_DATA_IN[dispatched]) {
                if (G_io_apdu_buffer[ISO_OFFSET_LC] == 0x00 ||
                    btchip_context_D.inLength - 5 == 0) {
                    btchip_context_D.sw = BTCHIP_SW_INCORRECT_LENGTH;
                    goto sendSW;
                }
                // notify we need to receive data
                // io_exchange(CHANNEL_APDU | IO_RECEIVE_DATA, 0);
            }
            // call the apdu handler
            btchip_context_D.sw = ((apduProcessingFunction)PIC(
                DISPATCHER_FUNCTIONS[dispatched]))();

// an APDU has been replied. request for power off time extension from the
// common ux
#ifdef IO_APP_ACTIVITY
            IO_APP_ACTIVITY();
#endif // IO_APP_ACTIVITY

        sendSW:
            if (btchip_context_D.called_from_swap) {
                btchip_context_D.io_flags &= ~IO_ASYNCH_REPLY;
                if(btchip_context_D.sw != BTCHIP_SW_OK) {
                    vars.swap_data.should_exit = 1;
                }
            }
            // prepare SW after replied data
            G_io_apdu_buffer[btchip_context_D.outLength] =
                (btchip_context_D.sw >> 8);
            G_io_apdu_buffer[btchip_context_D.outLength + 1] =
                (btchip_context_D.sw & 0xff);
            btchip_context_D.outLength += 2;

            // legacy APDUs use IO_ASYNCH_REPLY to signal that no response should be sent
            if ((btchip_context_D.io_flags & IO_ASYNCH_REPLY) == 0) {
                // send response                
                io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, btchip_context_D.outLength);
            }
        }
        CATCH(EXCEPTION_IO_RESET) {
            THROW(EXCEPTION_IO_RESET);
        }
        CATCH_OTHER(e) {
            // uncaught exception detected
            G_io_apdu_buffer[0] = 0x6F;
            btchip_context_D.outLength = 2;
            G_io_apdu_buffer[1] = e;
            // we caught something suspicious
            SB_SET(btchip_context_D.halted, 1);
        }
        FINALLY;
    }
    END_TRY;
}

// void app_main(void) {
//     os_memset(G_io_apdu_buffer, 0, 255); // paranoia

//     // Process the incoming APDUs

//     // first exchange, no out length :) only wait the apdu
//     btchip_context_D.outLength = 0;
//     btchip_context_D.io_flags = 0;
//     for (;;) {

//         // os_memset(G_io_apdu_buffer, 0, 255); // paranoia

//         if (btchip_context_D.called_from_swap && vars.swap_data.should_exit) {
//             btchip_context_D.io_flags |= IO_RETURN_AFTER_TX;
//         }

//         // receive the whole apdu using the 7 bytes headers (ledger transport)
//         btchip_context_D.inLength =
//             io_exchange(CHANNEL_APDU | btchip_context_D.io_flags,
//                         // use the previous outlength as the reply
//                         btchip_context_D.outLength);

//         if (btchip_context_D.called_from_swap && vars.swap_data.should_exit) {
//             os_sched_exit(0);
//         }

//         PRINTF("New APDU received:\n%.*H\n", btchip_context_D.inLength, G_io_apdu_buffer);

//         app_dispatch();

//         // reply during reception of next apdu
//     }

//     PRINTF("End of main loop\n");

//     // in case reached
//     reset();
// }
