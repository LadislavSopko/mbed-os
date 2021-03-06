/*
 * Copyright (c) 2018, Arm Limited and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#if !defined(MBED_CONF_NSAPI_PRESENT)
#error [NOT_SUPPORTED] A json configuration file is needed. Skipping this build.
#endif

#include "CellularUtil.h" // for CELLULAR_ helper macros
#include "CellularTargets.h"

#ifndef CELLULAR_DEVICE
#error [NOT_SUPPORTED] CELLULAR_DEVICE must be defined
#endif

#ifndef MBED_CONF_APP_CELLULAR_SIM_PIN
#error [NOT_SUPPORTED] SIM pin code is needed. Skipping this build.
#endif

#if defined(TARGET_ADV_WISE_1570) || defined(TARGET_MTB_ADV_WISE_1570)
#error [NOT_SUPPORTED] target MTB_ADV_WISE_1570 is too unstable for network tests, IoT network is unstable
#endif

#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"

#include "mbed.h"

#include "AT_CellularNetwork.h"
#include "CellularContext.h"
#include "CellularDevice.h"
#include "../../cellular_tests_common.h"
#include CELLULAR_STRINGIFY(CELLULAR_DEVICE.h)

#define NETWORK_TIMEOUT (180*1000)

static CellularContext *ctx;
static CellularDevice *device;
static CellularNetwork *nw;

static void init_network_interface()
{
    ctx = CellularContext::get_default_instance();
    TEST_ASSERT(ctx != NULL);
    ctx->set_sim_pin(MBED_CONF_APP_CELLULAR_SIM_PIN);
#ifdef MBED_CONF_APP_APN
    ctx->set_credentials(MBED_CONF_APP_APN);
#endif
    TEST_ASSERT(ctx->register_to_network() == NSAPI_ERROR_OK);

    device = CellularDevice::get_default_instance();
    TEST_ASSERT(device != NULL);
}

static bool get_network_registration(CellularNetwork::RegistrationType type,
                                     CellularNetwork::RegistrationStatus &status, bool &is_registered)
{
    is_registered = false;
    CellularNetwork::registration_params_t reg_params;
    nsapi_error_t err = nw->get_registration_params(type, reg_params);
    TEST_ASSERT(err == NSAPI_ERROR_OK || err == NSAPI_ERROR_UNSUPPORTED);

    status = reg_params._status;
    switch (status) {
        case CellularNetwork::RegisteredRoaming:
        case CellularNetwork::RegisteredHomeNetwork:
            is_registered = true;
            break;
        case CellularNetwork::RegisteredSMSOnlyRoaming:
        case CellularNetwork::RegisteredSMSOnlyHome:
        case CellularNetwork::RegisteredCSFBNotPreferredRoaming:
        case CellularNetwork::RegisteredCSFBNotPreferredHome:
        case CellularNetwork::AttachedEmergencyOnly:
        case CellularNetwork::RegistrationDenied:
        case CellularNetwork::NotRegistered:
        case CellularNetwork::Unknown:
        case CellularNetwork::SearchingNetwork:
        default:
            break;
    }
    return true;
}

static bool is_registered()
{
    CellularNetwork::RegistrationStatus status;
    bool is_registered = false;

    for (int type = 0; type < CellularNetwork::C_MAX; type++) {
        if (get_network_registration((CellularNetwork::RegistrationType) type, status, is_registered)) {
            if (is_registered) {
                break;
            }
        }
    }

    return is_registered;
}

static void nw_callback(nsapi_event_t ev, intptr_t intptr)
{
}

static void test_network_registration()
{
    device->set_timeout(10 * 1000);
    nw = device->open_network();
    TEST_ASSERT(nw != NULL);

    nw->attach(&nw_callback);

    bool success = false;
    for (int type = 0; type < CellularNetwork::C_MAX; type++) {
        if (!nw->set_registration_urc((CellularNetwork::RegistrationType)type, true)) {
            success = true;
        }
    }
    // each modem should support at least one registration type
    TEST_ASSERT(success);

    int sanity_count = 0;
    while (is_registered() == false) {
        if (sanity_count == 0) {
            TEST_ASSERT(nw->set_registration() == NSAPI_ERROR_OK);
        }
        sanity_count++;
        wait(2);
        TEST_ASSERT(sanity_count < 60);
    }

    // device was registered right away, test set_registration call
    if (sanity_count == 0) {
        TEST_ASSERT(nw->set_registration() == NSAPI_ERROR_OK);
    }

    CellularNetwork::NWRegisteringMode reg_mode = CellularNetwork::NWModeDeRegister;
    TEST_ASSERT(nw->get_network_registering_mode(reg_mode) == NSAPI_ERROR_OK);
    TEST_ASSERT(reg_mode == CellularNetwork::NWModeAutomatic);
}

static void test_attach()
{
    wait(10);
    TEST_ASSERT(nw->set_attach() == NSAPI_ERROR_OK);

    CellularNetwork::AttachStatus status;
    TEST_ASSERT(nw->get_attach(status) == NSAPI_ERROR_OK);
    TEST_ASSERT(status == CellularNetwork::Attached);
}

static void test_other()
{
    const char *devi = CELLULAR_STRINGIFY(CELLULAR_DEVICE);
    TEST_ASSERT(nw->get_3gpp_error() == 0);

    nsapi_error_t err = nw->set_access_technology(CellularNetwork::RAT_GSM);
    TEST_ASSERT(err == NSAPI_ERROR_OK || err == NSAPI_ERROR_UNSUPPORTED);

    // scanning of operators requires some delay before operation is allowed(seen with WISE_1570)
    wait(5);
    // scanning of operators might take a long time
    device->set_timeout(240 * 1000);
    CellularNetwork::operList_t operators;
    int uplinkRate = -1;
    TEST_ASSERT(nw->scan_plmn(operators, uplinkRate) == NSAPI_ERROR_OK);
    device->set_timeout(10 * 1000);

    int rxlev = -1, ber = -1, rscp = -1, ecno = -1, rsrq = -1, rsrp = -1;
    err = nw->get_extended_signal_quality(rxlev, ber, rscp, ecno, rsrq, rsrp);
    TEST_ASSERT(err == NSAPI_ERROR_OK || err == NSAPI_ERROR_DEVICE_ERROR);
    if (err == NSAPI_ERROR_DEVICE_ERROR) {
        if (strcmp(devi, "QUECTEL_BG96") != 0 && strcmp(devi, "TELIT_HE910") != 0) {// QUECTEL_BG96 does not give any specific reason for device error
            TEST_ASSERT((((AT_CellularNetwork *)nw)->get_device_error().errType == 3) &&   // 3 == CME error from the modem
                        ((((AT_CellularNetwork *)nw)->get_device_error().errCode == 100) || // 100 == unknown command for modem
                         (((AT_CellularNetwork *)nw)->get_device_error().errCode == 50)));  // 50 == incorrect parameters // seen in wise_1570 for not supported commands
        }
    } else {
        // we should have some values which are not optional
        TEST_ASSERT(rxlev >= 0 && ber >= 0 && rscp >= 0 && ecno >= 0 && rsrq >= 0 && rsrp >= 0);
    }

    int rssi = -1;
    ber = -1;
    err = nw->get_signal_quality(rssi, ber);
    TEST_ASSERT(err == NSAPI_ERROR_OK || err == NSAPI_ERROR_DEVICE_ERROR);
    if (err == NSAPI_ERROR_DEVICE_ERROR) {
        TEST_ASSERT((((AT_CellularNetwork *)nw)->get_device_error().errType == 3) &&   // 3 == CME error from the modem
                    ((((AT_CellularNetwork *)nw)->get_device_error().errCode == 100) || // 100 == unknown command for modem
                     (((AT_CellularNetwork *)nw)->get_device_error().errCode == 50)));  // 50 == incorrect parameters // seen in wise_1570 for not supported commands
    } else {
        // test for values
        TEST_ASSERT(rssi >= 0);
        TEST_ASSERT(ber >= 0);
    }

    CellularNetwork::registration_params_t reg_params;
    reg_params._cell_id = -5;
    TEST_ASSERT(nw->get_registration_params(reg_params) == NSAPI_ERROR_OK);
    TEST_ASSERT(reg_params._cell_id != -5);

    int format = -1;
    CellularNetwork::operator_t operator_params;
    // all params are optional so can't test operator_params
    err = nw->get_operator_params(format, operator_params);
    TEST_ASSERT(err == NSAPI_ERROR_OK);

    nsapi_connection_status_t st =  nw->get_connection_status();
    TEST_ASSERT(st == NSAPI_STATUS_DISCONNECTED);

#ifndef TARGET_UBLOX_C027 // AT command is supported, but excluded as it runs out of memory easily (there can be very many operator names)
    if (strcmp(devi, "QUECTEL_BG96") != 0 && strcmp(devi, "SARA4_PPP") != 0) {
        // QUECTEL_BG96 timeouts with this one, tested with 3 minute timeout
        CellularNetwork::operator_names_list op_names;
        err = nw->get_operator_names(op_names);
        TEST_ASSERT(err == NSAPI_ERROR_OK || err == NSAPI_ERROR_DEVICE_ERROR);
        if (err == NSAPI_ERROR_DEVICE_ERROR) {
            // if device error then we must check was that really device error or that modem/network does not support the commands
            TEST_ASSERT((((AT_CellularNetwork *)nw)->get_device_error().errType == 3) &&   // 3 == CME error from the modem
                        ((((AT_CellularNetwork *)nw)->get_device_error().errCode == 4) ||   // 4 == NOT SUPPORTED BY THE MODEM
                         (((AT_CellularNetwork *)nw)->get_device_error().errCode == 50)));  // 50 == incorrect parameters // seen in wise_1570 for not supported commands
        } else {
            CellularNetwork::operator_names_t *opn = op_names.get_head();
            TEST_ASSERT(strlen(opn->numeric) > 0);
            TEST_ASSERT(strlen(opn->alpha) > 0);
        }
    }
#endif

    // TELIT_HE910 and QUECTEL_BG96 just gives an error and no specific error number so we can't know is this real error or that modem/network does not support the command
    CellularNetwork::Supported_UE_Opt supported_opt = CellularNetwork::SUPPORTED_UE_OPT_MAX;
    CellularNetwork::Preferred_UE_Opt preferred_opt = CellularNetwork::PREFERRED_UE_OPT_MAX;
    err = nw->get_ciot_optimization_config(supported_opt, preferred_opt);
    TEST_ASSERT(err == NSAPI_ERROR_OK || err == NSAPI_ERROR_DEVICE_ERROR);
    if (err == NSAPI_ERROR_DEVICE_ERROR) {
        // if device error then we must check was that really device error or that modem/network does not support the commands
        if (!(strcmp(devi, "TELIT_HE910") == 0 || strcmp(devi, "QUECTEL_BG96") == 0 || strcmp(devi, "SARA4_PPP") == 0)) {
            TEST_ASSERT((((AT_CellularNetwork *)nw)->get_device_error().errType == 3) &&   // 3 == CME error from the modem
                        ((((AT_CellularNetwork *)nw)->get_device_error().errCode == 100) || // 100 == unknown command for modem
                         (((AT_CellularNetwork *)nw)->get_device_error().errCode == 50)));  // 50 == incorrect parameters // seen in wise_1570 for not supported commands
        }
    } else {
        TEST_ASSERT(supported_opt != CellularNetwork::SUPPORTED_UE_OPT_MAX);
        TEST_ASSERT(preferred_opt != CellularNetwork::PREFERRED_UE_OPT_MAX);
    }

    err = nw->set_ciot_optimization_config(supported_opt, preferred_opt);
    TEST_ASSERT(err == NSAPI_ERROR_OK || err == NSAPI_ERROR_DEVICE_ERROR);
    if (err == NSAPI_ERROR_DEVICE_ERROR) {
        // if device error then we must check was that really device error or that modem/network does not support the commands
        if (!(strcmp(devi, "TELIT_HE910") == 0 || strcmp(devi, "QUECTEL_BG96") == 0 || strcmp(devi, "SARA4_PPP") == 0)) {
            TEST_ASSERT((((AT_CellularNetwork *)nw)->get_device_error().errType == 3) &&   // 3 == CME error from the modem
                        ((((AT_CellularNetwork *)nw)->get_device_error().errCode == 100) || // 100 == unknown command for modem
                         (((AT_CellularNetwork *)nw)->get_device_error().errCode == 50)));  // 50 == incorrect parameters // seen in wise_1570 for not supported commands
        }
    }
}

static void test_detach()
{
    // in PPP mode there is NO CARRIER waiting so flush it out
    rtos::ThisThread::sleep_for(6 * 1000);
    ((AT_CellularNetwork *)nw)->get_at_handler().flush();

    nsapi_connection_status_t st =  nw->get_connection_status();
    TEST_ASSERT(st == NSAPI_STATUS_DISCONNECTED);

    TEST_ASSERT(nw->detach() == NSAPI_ERROR_OK);
    // wait to process URC's, received after detach
    rtos::ThisThread::sleep_for(500);
    st =  nw->get_connection_status();
    TEST_ASSERT(st == NSAPI_STATUS_DISCONNECTED);
}

using namespace utest::v1;

static utest::v1::status_t greentea_failure_handler(const Case *const source, const failure_t reason)
{
    greentea_case_failure_abort_handler(source, reason);
    return STATUS_ABORT;
}

static Case cases[] = {
    Case("CellularNetwork init", init_network_interface, greentea_failure_handler),
    Case("CellularNetwork test registering", test_network_registration, greentea_failure_handler),
    Case("CellularNetwork test attach", test_attach, greentea_failure_handler),
    Case("CellularNetwork test other functions", test_other, greentea_failure_handler),
    Case("CellularNetwork test detach", test_detach, greentea_failure_handler)
};

static utest::v1::status_t test_setup(const size_t number_of_cases)
{
    GREENTEA_SETUP(10 * 60, "default_auto");
    return verbose_test_setup_handler(number_of_cases);
}

static Specification specification(test_setup, cases);

int main()
{
#if MBED_CONF_MBED_TRACE_ENABLE
    trace_open();
#endif
    int ret = Harness::run(specification);
#if MBED_CONF_MBED_TRACE_ENABLE
    trace_close();
#endif
    return ret;
}
