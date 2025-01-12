# SPDX-License-Identifier: GPL-2.0-only */
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#

if [ -z "${PITRAC_ROOT}" ]; then
  echo "PITRAC_ROOT environment variable not set.  Exiting."
  exit
else
  echo "PITRAC_ROOT set to: ${PITRAC_ROOT}"
fi

if [ -z "${PITRAC_MSG_BROKER_FULL_ADDRESS}" ]; then
  echo "PITRAC_MSG_BROKER_FULL_ADDRESS environment variable not set.  Exiting."
  exit
else
  echo "PITRAC_MSG_BROKER_FULL_ADDRESS set to: ${PITRAC_MSG_BROKER_FULL_ADDRESS}"
fi

if [ -z "${PITRAC_WEBSERVER_SHARE_DIR}" ]; then
  echo "PITRAC_WEBSERVER_SHARE_DIR environment variable not set.  Exiting."
  exit
else
  echo "PITRAC_WEBSERVER_SHARE_DIR set to: ${PITRAC_WEBSERVER_SHARE_DIR}"
fi

if [ -z "${PITRAC_BASE_IMAGE_LOGGING_DIR}" ]; then
  echo "PITRAC_BASE_IMAGE_LOGGING_DIR environment variable not set.  Exiting."
  exit
else
  echo "PITRAC_BASE_IMAGE_LOGGING_DIR set to: ${PITRAC_BASE_IMAGE_LOGGING_DIR}"
fi


if [ -z "${PITRAC_E6_HOST_ADDRESS}" ]; then
# If the E6 sim isn't in use, do nothing
  echo ""
else
  echo "PITRAC_E6_HOST_ADDRESS set to: ${PITRAC_E6_HOST_ADDRESS}"
  export PITRAC_SIM_ARG="--e6_host_address $PITRAC_E6_HOST_ADDRESS"
fi

if [ -z "${PITRAC_GSPRO_HOST_ADDRESS}" ]; then
# If the GSPRO sim isn't in use, do nothing
  echo ""
else
  echo "PITRAC_GSPRO_HOST_ADDRESS set to: ${PITRAC_GSPRO_HOST_ADDRESS}"
  export PITRAC_SIM_ARG="--gspro_host_address $PITRAC_GSPRO_HOST_ADDRESS"
fi


# NOTE that the PITRAC_SIM_ARG may be empty
export PITRAC_COMMON_CMD_LINE_ARGS=" --config_file $PITRAC_ROOT/ImageProcessing/golf_sim_config.json --msg_broker_address $PITRAC_MSG_BROKER_FULL_ADDRESS $PITRAC_SIM_ARG --web_server_share_dir $PITRAC_WEBSERVER_SHARE_DIR --base_image_logging_dir $PITRAC_BASE_IMAGE_LOGGING_DIR "

echo "PITRAC_COMMON_CMD_LINE_ARGS = " $PITRAC_COMMON_CMD_LINE_ARGS

# Ensure that any images that will be displayed can show up on any connected monitor
export DISPLAY=:0.0

