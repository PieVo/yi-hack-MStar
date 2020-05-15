#!/bin/sh

# Conf
CONF_FILE="etc/camera.conf"
YI_HACK_PREFIX="/home/yi-hack"

get_config()
{
    key=$1
    grep -w $1 $YI_HACK_PREFIX/$CONF_FILE | cut -d "=" -f2
}

# Files
TMPOUT=/tmp/config.7z.dl
TMPDIR=/tmp/workdir.tmp
TMPOUT7z=$TMPDIR/config.7z

# Cleaning
rm -f $TMPOUT
rm -f $TMPOUT7z
rm -rf $TMPDIR

mkdir -p $TMPDIR

if [ $CONTENT_LENGTH -gt 10000 ]; then
    exit
fi

if [ "$REQUEST_METHOD" = "POST" ]; then
    cat >$TMPOUT

    # Get the line count
    LINES=$(grep -c "" $TMPOUT)

    touch $TMPOUT7z
    l=1
    LENSKIP=0

    # Process post data removing head and tail
    while true; do
        if [ $l -eq 1 ]; then
            ROW=`cat $TMPOUT | awk "FNR == $l {print}"`
            BOUNDARY=${#ROW}
            BOUNDARY=$((BOUNDARY+1))
            LENSKIPSTART=$BOUNDARY
            LENSKIPEND=$BOUNDARY
        elif [ $l -le 4 ]; then
            ROW=`cat $TMPOUT | awk "FNR == $l {print}"`
            ROWLEN=${#ROW}
            LENSKIPSTART=$((LENSKIPSTART+ROWLEN+1))
        elif [ \( $l -gt 4 \) -a \( $l -lt $LINES \) ]; then
            ROW=`cat $TMPOUT | awk "FNR == $l {print}"`
        else
            break
        fi
        l=$((l+1))
    done
fi

# Extract 7z file
LEN=$((CONTENT_LENGTH-LENSKIPSTART-LENSKIPEND+2))
dd if=$TMPOUT of=$TMPOUT7z bs=1 skip=$LENSKIPSTART count=$LEN >/dev/null 2>&1
cd $TMPDIR
7za x -y $TMPOUT7z >/dev/null 2>&1
RES=$?

# Verify result of 7za command and copy files to destination
if [ $RES -eq 0 ]; then
    if [ \( -f "system.conf" \) -a \( -f "camera.conf" \) ]; then
        mv -f *.conf /home/yi-hack/etc/
        mv -f TZ /home/yi-hack/etc/
        chmod 0644 /home/yi-hack/etc/*.conf
        chmod 0644 /home/yi-hack/etc/TZ
        RES=0
    else
        RES=1
    fi
fi

# Cleaning
cd ..
rm -rf $TMPDIR
rm -f $TMPOUT
rm -f $TMPOUT7z

# Print response
printf "Content-type: text/html\r\n\r\n"
if [ $RES -eq 0 ]; then
    printf "Upload completed successfully, restart your camera\r\n"
else
    printf "Upload failed\r\n"
fi

if [ ! -f "$YI_HACK_PREFIX/$CONF_FILE" ]; then
    exit
fi

# Set camera settings
if [[ $(get_config SWITCH_ON) == "no" ]] ; then
    ipc_cmd -t off
else
    ipc_cmd -t on
fi

if [[ $(get_config SAVE_VIDEO_ON_MOTION) == "no" ]] ; then
    ipc_cmd -v always
else
    ipc_cmd -v detect
fi

ipc_cmd -s $(get_config SENSITIVITY)

if [[ $(get_config LED) == "no" ]] ; then
    ipc_cmd -l off
else
    ipc_cmd -l on
fi

if [[ $(get_config IR) == "no" ]] ; then
    ipc_cmd -i off
else
    ipc_cmd -i on
fi

if [[ $(get_config ROTATE) == "no" ]] ; then
    ipc_cmd -r off
else
    ipc_cmd -r o
fi
