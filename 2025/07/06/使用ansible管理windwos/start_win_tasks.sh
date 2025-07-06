#!/bin/bash

if [ -z "$1" ]; then
  echo "Usage: $0 <value_to_pass>"
  exit 1
fi

VARIABLE_VALUE="$1"

PLAYBOOK="/root/cuebot/change_rqd_tags.yaml" 
INVENTORY="/etc/ansible/cuebot_inventory.yml"       

echo "Passing value '${VARIABLE_VALUE}' to the playbook."
echo "Running playbook: ${PLAYBOOK}"
echo "Using inventory: ${INVENTORY}"
JSON_VARS="{ \"env_var_value\": \"${VARIABLE_VALUE}\"}"

# 执行 ansible-playbook 命令，使用 -e 或 --extra-vars 传递变量
# 格式可以是 key=value
ansible-playbook -vi "${INVENTORY}" "${PLAYBOOK}" --extra-vars "${JSON_VARS}"
# ansible-playbook -i "${INVENTORY}" "${PLAYBOOK}" -e "var1=${VALUE1} var2=${VALUE2}"

if [ $? -eq 0 ]; then
  echo "Ansible playbook completed successfully."
  exit 0
else
  echo "Ansible playbook failed."
  exit 1
fi