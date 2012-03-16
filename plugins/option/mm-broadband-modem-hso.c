/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your hso) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-modem-helpers.h"
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-hso.h"
#include "mm-broadband-bearer-hso.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemHso, mm_broadband_modem_hso, MM_TYPE_BROADBAND_MODEM_OPTION, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init));

struct _MMBroadbandModemHsoPrivate {
    /* Regex for connected notifications */
    GRegex *_owancall_regex;
};

/*****************************************************************************/
/* Create Bearer (Modem interface) */

static MMBearer *
modem_create_bearer_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    MMBearer *bearer;

    bearer = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    mm_dbg ("New HSO bearer created at DBus path '%s'", mm_bearer_get_path (bearer));

    return g_object_ref (bearer);
}

static void
broadband_bearer_hso_new_ready (GObject *source,
                                GAsyncResult *res,
                                GSimpleAsyncResult *simple)
{
    MMBearer *bearer = NULL;
    GError *error = NULL;

    bearer = mm_broadband_bearer_hso_new_finish (res, &error);
    if (!bearer)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   bearer,
                                                   (GDestroyNotify)g_object_unref);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_create_bearer (MMIfaceModem *self,
                     MMBearerProperties *properties,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_create_bearer);

    mm_dbg ("Creating HSO bearer...");
    mm_broadband_bearer_hso_new (MM_BROADBAND_MODEM_HSO (self),
                                 properties,
                                 NULL, /* cancellable */
                                 (GAsyncReadyCallback)broadband_bearer_hso_new_ready,
                                 result);
}
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_hso_parent_class)->setup_ports (self);

    /* _OWANCALL unsolicited messages are only expected in the primary port. */
    mm_at_serial_port_add_unsolicited_msg_handler (
        mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
        MM_BROADBAND_MODEM_HSO (self)->priv->_owancall_regex,
        NULL, NULL, NULL);

    g_object_set (mm_base_modem_peek_port_primary (MM_BASE_MODEM (self)),
                  MM_SERIAL_PORT_SEND_DELAY, (guint64) 0,
                  /* built-in echo removal conflicts with unsolicited _OWANCALL
                   * messages, which are not <CR><LF> prefixed. */
                  MM_AT_SERIAL_PORT_REMOVE_ECHO, FALSE,
                  NULL);
}

/*****************************************************************************/

MMBroadbandModemHso *
mm_broadband_modem_hso_new (const gchar *device,
                              const gchar *driver,
                              const gchar *plugin,
                              guint16 vendor_id,
                              guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_HSO,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
finalize (GObject *object)
{
    MMBroadbandModemHso *self = MM_BROADBAND_MODEM_HSO (object);

    g_regex_unref (self->priv->_owancall_regex);

    G_OBJECT_CLASS (mm_broadband_modem_hso_parent_class)->finalize (object);
}

static void
mm_broadband_modem_hso_init (MMBroadbandModemHso *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_HSO,
                                              MMBroadbandModemHsoPrivate);

    self->priv->_owancall_regex = g_regex_new ("_OWANCALL: (\\d),\\s*(\\d)\\r\\n",
                                               G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->create_bearer = modem_create_bearer;
    iface->create_bearer_finish = modem_create_bearer_finish;
}

mm_broadband_modem_hso_class_init (MMBroadbandModemHsoClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemHsoPrivate));

    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;
}