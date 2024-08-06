#!/bin/bash

# ./scripts/gfs-lvm-backup.sh - this file is a part of program blocksync-fast

# Copyright (C) 2024 Marcin Koczwara <mk@nethorizon.pl>

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

YEAR=$(date +%Y)
MONTH=$(date +%m)
DAY_OF_MONTH=$(date +%d)
DAY_OF_WEEK=$(date +%u)
HOUR=$(date +%H)
MINUTE=$(date +%M)

BACKUPTIME="$YEAR-$MONTH-${DAY_OF_MONTH}_$HOUR:$MINUTE"

export PATH=$PATH:/usr/local/bin:/usr/local/sbin

mountDstDir()
{
    logger "$0 - starting mount $DESTDIR";
    sync;
    (mount | grep $DESTDIR  > /dev/null 2>&1) || (mount $DESTDIR)
    if [ "$?" != "0" ]; then
    logger "$0 - mounting $DESTDIR failed";
    exit 1;
    fi
    logger "$0 - mounting $DESTDIR success";
}

umountDstDir()
{
    logger "$0 - starting umount $DESTDIR";
    sync;
    (mount | grep $DESTDIR  > /dev/null 2>&1) && (umount $DESTDIR)
    if [ "$?" != "0" ]; then
        logger "$0 - unmounting $DESTDIR failed";
        exit 1;
    fi
    logger "$0 - unmounting $DESTDIR success";
}

createSnapshot()
{
    logger "$0 - starting create snapshot $VG/$LV-$SNAP_NAME";
    sync;
    (lvcreate -L$SNAP_SIZE -s -n $LV-$SNAP_NAME $VG/$LV > /dev/null 2>&1)
    if [ "$?" != "0" ]; then
    logger "$0 - creating snapshot $VG/$LV-$SNAP_NAME failed";
    exit 1;
    fi
    logger "$0 - creating snapshot $VG/$LV-$SNAP_NAME success";
}


removeSnapshot()
{
    logger "$0 - starting remove snapshot $VG/$LV-$SNAP_NAME";
    sync;
    [ ! -e "/dev/$VG/$LV-$SNAP_NAME" ] || (lvremove -f $VG/$LV-$SNAP_NAME > /dev/null 2>&1)
    if [ "$?" != "0" ]; then
    logger "$0 - removing snapshot $VG/$LV-$SNAP_NAME failed";
    exit 1;
    fi
    logger "$0 - removing snapshot $VG/$LV-$SNAP_NAME success";
}

makeBackup()
{
    logger "$0 - starting blocksync $VG/$LV";
    sync;
    [ -d "$DESTDIR/$LV" ] || (mkdir "$DESTDIR/$LV" > /dev/null 2>&1)
    LOG=$(blocksync-fast --src=/dev/$VG/$LV-$SNAP_NAME --dst=$DESTDIR/$LV/current --digest=$DIGEST_PATH)

    if [ "$?" != "0" ]; then
    logger "$0 - blocksyncing $VG/$LV failed";
    else
        echo $(date) > "$DESTDIR/$LV/current.date"
    logger "$0 - blocksync-fast $VG/$LV : $LOG";
    sync;
    logger "$0 - blocksyncing $VG/$LV success";
    fi;
}


rotateBackupDaily()
{
    logger "$0 - checking for daily rotate of $VG/$LV";
    sync;
    if [ $ROTATE_DAYS -gt 0 ]; then

    logger "$0 - starting copy with reflink daily backup $VG/$LV";
    
        [ -d "$DESTDIR/$LV/daily" ] || (mkdir "$DESTDIR/$LV/daily" > /dev/null 2>&1)
    cp --reflink "$DESTDIR/$LV/current" "$DESTDIR/$LV/daily/$LV-$BACKUPTIME"
    
    if [ "$?" != "0" ]; then
        logger "$0 - copying reflink $DESTDIR/$LV/current to $DESTDIR/$LV/daily/$LV-$BACKUPTIME failed";
    else
        logger "$0 - copying reflink $DESTDIR/$LV/current to $DESTDIR/$LV/daily/$LV-$BACKUPTIME success";
    fi;

    logger "$0 - cleaning backups of $DESTDIR/$LV/daily";
    i=0
    for x in $(ls $DESTDIR/$LV/daily | sort -r); do

        if [ $i -ge $ROTATE_DAYS ]; then
	logger "$0 - trying remove file $DESTDIR/$LV/daily/$x";
	rm "$DESTDIR/$LV/daily/$x";

	if [ "$?" != "0" ]; then
	    logger "$0 - removing file $DESTDIR/$LV/daily/$x failed";
	else
	    logger "$0 - removing file $DESTDIR/$LV/daily/$x success";
	fi;
    
        fi
        ((i++))
    done;
    fi;
    logger "$0 - daily rotate of $VG/$LV finish";
}

rotateBackupWeekly()
{
    logger "$0 - checking for weekly rotate of $VG/$LV";
    sync;
    if [ $ROTATE_WEEKS -gt 0 ] && [ $DAY_OF_WEEK -eq 1 ]; then

    logger "$0 - starting copy with reflink weekly backup $VG/$LV";
    
        [ -d "$DESTDIR/$LV/weekly" ] || (mkdir "$DESTDIR/$LV/weekly" > /dev/null 2>&1)
    cp --reflink "$DESTDIR/$LV/current" "$DESTDIR/$LV/weekly/$LV-$BACKUPTIME"

    if [ "$?" != "0" ]; then
        logger "$0 - copying reflink $DESTDIR/$LV/current to $DESTDIR/$LV/weekly/$LV-$BACKUPTIME failed";
    else
        logger "$0 - copying reflink $DESTDIR/$LV/current to $DESTDIR/$LV/weekly/$LV-$BACKUPTIME success";
    fi;

    logger "$0 - cleaning backups of $DESTDIR/$LV/weekly";
    i=0
    for x in $(ls $DESTDIR/$LV/weekly | sort -r); do

        if [ $i -ge $ROTATE_WEEKS ]; then
	logger "$0 - trying remove file $DESTDIR/$LV/weekly/$x";
	rm "$DESTDIR/$LV/weekly/$x"
	
	if [ "$?" != "0" ]; then
	    logger "$0 - removing file $DESTDIR/$LV/weekly/$x failed";
	else
	    logger "$0 - removing file $DESTDIR/$LV/weekly/$x success";
	fi;
        fi
        ((i++))
    done;
    fi;
    logger "$0 - weekly rotate of $VG/$LV finish";
}

rotateBackupMonthly()
{
    logger "$0 - checking for monthly rotate of $VG/$LV";
    sync;
    if [ $ROTATE_MONTHS -gt 0 ] && [ $DAY_OF_MONTH -eq 1 ]; then

    logger "$0 - starting copy with reflink monthly backup $VG/$LV";

        [ -d "$DESTDIR/$LV/monthly" ] || (mkdir "$DESTDIR/$LV/monthly" > /dev/null 2>&1)
    cp --reflink "$DESTDIR/$LV/current" "$DESTDIR/$LV/monthly/$LV-$BACKUPTIME"

    if [ "$?" != "0" ]; then
        logger "$0 - copying reflink $DESTDIR/$LV/current to $DESTDIR/$LV/monthly/$LV-$BACKUPTIME failed";
    else
        logger "$0 - copying reflink $DESTDIR/$LV/current to $DESTDIR/$LV/monthly/$LV-$BACKUPTIME success";
    fi;

    logger "$0 - cleaning backups of $DESTDIR/$LV/monthly";
    i=0
    for x in $(ls $DESTDIR/$LV/monthly | sort -r); do

        if [ $i -ge $ROTATE_MONTHS ]; then
	logger "$0 - trying remove file $DESTDIR/$LV/monthly/$x";
	rm "$DESTDIR/$LV/monthly/$x"

	if [ "$?" != "0" ]; then
	    logger "$0 - removing file $DESTDIR/$LV/monthly/$x failed";
	else
	    logger "$0 - removing file $DESTDIR/$LV/monthly/$x success";
	fi;
        fi
        ((i++))
    done;
    fi;
    logger "$0 - monthly rotate of $VG/$LV finish";
}

logger "$0 - STARTING BACKUP SCRIPT OF $VG/$LV";

removeSnapshot;
mountDstDir;
createSnapshot;
makeBackup;
removeSnapshot;

rotateBackupDaily;
rotateBackupWeekly;
rotateBackupMonthly;
umountDstDir;

logger "$0 - BACKUP SCRIPT OF $VG/$LV FINISH";
