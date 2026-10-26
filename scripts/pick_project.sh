#!/usr/bin/env bash
# Печатает имя проекта дня (ротация по номеру дня в году).
cd "$(dirname "$0")/../projects" || exit 1
mapfile -t list < <(ls *.md | grep -v '^_' | sed 's/\.md$//' | sort)
echo "${list[$(( 10#$(date +%j) % ${#list[@]} ))]}"
