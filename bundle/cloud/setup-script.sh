#!/bin/bash
# The cloud environment's setup script (claude.ai, environment settings): ait needs
# git 2.53 or later and Ubuntu 24.04 has 2.43, so take git from the git-core PPA.
# add-apt-repository asks api.launchpad.net, which the Trusted network does not reach;
# the list and key are written here instead (key from keyserver.ubuntu.com).
set -e
install -d -m 0755 /etc/apt/keyrings
curl -fsSL 'https://keyserver.ubuntu.com/pks/lookup?op=get&search=0xF911AB184317630C59970973E363C90F8F1B6217' |
	gpg --dearmor --yes -o /etc/apt/keyrings/git-core.gpg
echo 'deb [signed-by=/etc/apt/keyrings/git-core.gpg] https://ppa.launchpadcontent.net/git-core/ppa/ubuntu noble main' \
	>/etc/apt/sources.list.d/git-core.list
apt-get update
apt-get install -y git
git --version
