#!/vendor/bin/sh
#
#
# Copyright (C) Intel 2015
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

DEFAULT_LOGFS_PROP=$( /vendor/bin/getprop ro.vendor.service.default_logfs )
APLOGFS_PROP=$( /vendor/bin/getprop persist.vendor.service.aplogfs.enable )
APKLOGFS_PROP=$( /vendor/bin/getprop persist.vendor.service.apklogfs.enable )
VENDOR_SETPROP=/vendor/bin/setprop

Set_prop() {
	if [ "$DEFAULT_LOGFS_PROP" = "apklogfs" ]
        then
		if [ ! -n "$APLOGFS_PROP" ]
                then
			$VENDOR_SETPROP persist.vendor.service.apklogfs.enable 1
		fi
	fi
	if [ "$DEFAULT_LOGFS_PROP" = "aplogfs" ]
        then
		if [ ! -n "$APKLOGFS_PROP"]
                then
			$VENDOR_SETPROP persist.vendor.service.aplogfs.enable 1
		fi
	fi
}

Set_prop
exit 0
