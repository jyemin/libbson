/*
 * Copyright 2013 10gen Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include "cbson-util.h"
#include "time64.h"

#include <datetime.h>


bson_bool_t
cbson_util_init (void)
{
   PyDateTime_IMPORT;
   if (!PyDateTimeAPI) {
      return FALSE;
   }

   return TRUE;
}


bson_bool_t
cbson_date_time_check (PyObject *object)
{
   return PyDateTime_Check(object);
}


PyObject *
cbson_date_time_from_msec (bson_int64_t msec_since_epoch)
{
   bson_int32_t diff;
   bson_int32_t microseconds;
   struct TM timeinfo;
   Time64_T seconds;

   /* To encode a datetime instance like datetime(9999, 12, 31, 23, 59, 59, 999999)
    * we follow these steps:
    * 1. Calculate a timestamp in seconds:       253402300799
    * 2. Multiply that by 1000:                  253402300799000
    * 3. Add in microseconds divided by 1000     253402300799999
    *
    * (Note: BSON doesn't support microsecond accuracy, hence the rounding.)
    *
    * To decode we could do:
    * 1. Get seconds: timestamp / 1000:          253402300799
    * 2. Get micros: (timestamp % 1000) * 1000:  999000
    * Resulting in datetime(9999, 12, 31, 23, 59, 59, 999000) -- the expected result
    *
    * Now what if the we encode (1, 1, 1, 1, 1, 1, 111111)?
    * 1. and 2. gives:                           -62135593139000
    * 3. Gives us:                               -62135593138889
    *
    * Now decode:
    * 1. Gives us:                               -62135593138
    * 2. Gives us:                               -889000
    * Resulting in datetime(1, 1, 1, 1, 1, 2, 15888216) -- an invalid result
    *
    * If instead to decode we do:
    * diff = ((millis % 1000) + 1000) % 1000:    111
    * seconds = (millis - diff) / 1000:          -62135593139
    * micros = diff * 1000                       111000
    * Resulting in datetime(1, 1, 1, 1, 1, 1, 111000) -- the expected result
    */

   diff = ((msec_since_epoch % 1000L) + 1000) % 1000;
   microseconds = diff * 1000;
   seconds = (msec_since_epoch - diff) / 1000;
   gmtime64_r(&seconds, &timeinfo);
   return PyDateTime_FromDateAndTime(timeinfo.tm_year + 1900,
                                     timeinfo.tm_mon + 1,
                                     timeinfo.tm_mday,
                                     timeinfo.tm_hour,
                                     timeinfo.tm_min,
                                     timeinfo.tm_sec,
                                     microseconds);
}


bson_int32_t
cbson_date_time_seconds (PyObject *date_time)
{
   bson_int32_t val = -1;
   PyObject *calendar;
   PyObject *ret;
   PyObject *timegm;
   PyObject *timetuple;
   PyObject *offset;

   /*
    * TODO: We can get about a 30% performance improvement for tight loops if
    *       the calendar module is cached. However, that complicates other
    *       issues like handling unexpected fork() by various modules.
    */

   bson_return_val_if_fail(PyDateTime_Check(date_time), 0);

   if (!(calendar = PyImport_ImportModule("calendar"))) {
      return 0;
   }

   if (!(timegm = PyObject_GetAttrString(calendar, "timegm"))) {
      goto dec_calendar;
   }

   if (!(offset = PyObject_CallMethod(date_time, (char *)"utcoffset", NULL))) {
      goto dec_timegm;
   }

   if (offset != Py_None) {
      date_time = PyNumber_Subtract(date_time, offset);
   } else {
      Py_INCREF(date_time);
   }

   if (!(timetuple = PyObject_CallMethod(date_time, (char *)"timetuple", NULL))) {
      goto dec_offset;
   }

   if (!(ret = PyObject_CallFunction(timegm, (char *)"O", timetuple))) {
      goto dec_timetuple;
   }

   val = PyInt_AS_LONG(ret);

   Py_DECREF(ret);

dec_timetuple:
   Py_DECREF(timetuple);
dec_offset:
   Py_DECREF(offset);
   Py_DECREF(date_time);
dec_timegm:
   Py_DECREF(timegm);
dec_calendar:
   Py_DECREF(calendar);

   return val;
}
