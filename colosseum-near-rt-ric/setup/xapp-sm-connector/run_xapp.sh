#!/bin/sh
export AI_HOST=${AI_HOST:-host.docker.internal}
export AI_PORT=${AI_PORT:-5000}

# If AI_HOST is the Docker host alias but it's not mapped, fall back to default gateway IP
if [ "$AI_HOST" = "host.docker.internal" ]; then
	# Try to resolve; if resolution fails, derive gateway IP from /proc/net/route
	if ! getent hosts "$AI_HOST" >/dev/null 2>&1; then
		GW_IP="$(awk '$2 == "00000000" {print strtonum("0x" substr($3,7,2)) "." strtonum("0x" substr($3,5,2)) "." strtonum("0x" substr($3,3,2)) "." strtonum("0x" substr($3,1,2)) }' /proc/net/route | head -n1)"
		if [ -n "$GW_IP" ]; then
			export AI_HOST="$GW_IP"
		fi
	fi
fi

cd /home/xapp-sm-connector/init/ && python3 init_script.py config-file.json
