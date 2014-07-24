#!/bin/bash

EMV_CGMOUNT="${EMV_CGMOUNT:-/sys/fs/cgroup}"
EMV_HOST="${EMV_HOST:-big}"
EMV_TRGT="${EMV_TRGT:-LITTLE}"
ENV_EXPS="${EMV_EXPS:-1 2 3 4 5}"
ENV_DRTS="${EMV_DRTS:-2 4 8 16 32}"

case $EMV_HOST in
	"big")
		# Test EM on LITTLE (3xA7) cluster
		EMV_HCPUS="0-1"
		EMV_TCPUS="2-4"
		;;
	*)
		# Test EM on big (2xA15) cluster
		EMV_HCPUS="2-4"
		EMV_TCPUS="0-1"
		;;
esac

################################################################################
# CGroup Shield Setup
################################################################################

# Check root permission
check_root() {
	# Make sure only root can run our script
	if [ "x`id -u`" != "x0" ]; then
		echo "This script must be run as root\n\n"
		exit 1
	fi
}

# Check mouting status of required controllers
check_controllers() {
	# The [cpuset] controller must be mounted
	CPUSET=`mount | grep cpuset`
	if [ "x$CPUSET" == x ]; then
		# Mount required [cpuset] hierarchy
		mount -t cgroup -o cpuset cpuset $EMV_CGMOUNT
		if [ $? -ne 0 ]; then
			echo 'Mounting [cpuset] hierarchy FAILED'
			echo 'EV-Validation could not be started without proper CGroups setup'
			exit 2
		fi
	fi
}

# Build up the Shield my moving all tasks into HOST node
shield_build() {
	echo "Moving all (user-space) tasks into $EMV_HOST cluster node..."
	local rotate='|/-\'

	# Setup HOST partition
	if [ ! -d $EMV_CGMOUNT/$EMV_HOST ]; then
		mkdir -p $EMV_CGMOUNT/$EMV_HOST
		echo $EMV_HCPUS > $EMV_CGMOUNT/$EMV_HOST/cpuset.cpus
		echo 0 > $EMV_CGMOUNT/$EMV_HOST/cpuset.mems
	fi

	# Setup TARGET partition
	if [ ! -d $EMV_CGMOUNT/$EMV_TRGT ]; then
		mkdir -p $EMV_CGMOUNT/$EMV_TRGT
		echo $EMV_TCPUS > $EMV_CGMOUNT/$EMV_TRGT/cpuset.cpus
		echo 0 > $EMV_CGMOUNT/$EMV_TRGT/cpuset.mems
	fi

	printf "Moving tasks to $EMV_HOST cluster ";
	for D in `find /proc/ -maxdepth 1 -type d`; do
			P=`basename $D`
			# jumping kernel thread which should not be moved
			readlink $D/exe >/dev/null 2>&1 || continue

			rotate="${rotate#?}${rotate%???}"
			printf "[%5d]... %.1s\b\b\b\b\b\b\b\b\b\b\b\b" $P  $rotate;
			echo $P > $EMV_CGMOUNT/$EMV_HOST/tasks
	done
	echo

}

shield_release() {

	[ -d $EMV_CGMOUNT/$EMV_HOST ] || return

	echo "Moving all (user-space) tasks back to ROOT node..."
	local rotate='|/-\'

	[ -f $EMV_CGMOUNT/$EMV_HOST/tasks ] && \
	printf "Moving tasks from $EMV_HOST to ROOT " && \
	cat $EMV_CGMOUNT/$EMV_HOST/tasks | sort -ru | \
	while read P; do
			rotate="${rotate#?}${rotate%???}"
			printf "[%5d]... %.1s\b\b\b\b\b\b\b\b\b\b\b\b" $P  $rotate;
			echo $P > $EMV_CGMOUNT/tasks 2>/dev/null
	done
	echo

	[ -f $EMV_CGMOUNT/$EMV_TRGT/tasks ] && \
	printf "Moving tasks from $EMV_TRGT to ROOT " && \
	cat $EMV_CGMOUNT/$EMV_TRGT/tasks | sort -ru | \
	while read P; do
			rotate="${rotate#?}${rotate%???}"
			printf "[%5d]... %.1s\b\b\b\b\b\b\b\b\b\b\b\b" $P  $rotate;
			echo $P > $EMV_CGMOUNT/tasks 2>/dev/null
	done
	echo

	echo "Releasing big.LITTLE CGroups..."
	rmdir $EMV_CGMOUNT/$EMV_HOST ||
		cat $EMV_CGMOUNT/$EMV_HOST/tasks && return

	rmdir $EMV_CGMOUNT/$EMV_TRGT ||
		cat $EMV_CGMOUNT/$EMV_TRGT/tasks && return


	umount $EMV_CGMOUNT
}

dump_conf() {
	echo "Current CGroups Configuration:"
	find /sys/fs/cgroup/ -type d | \
	while read D; do
		printf "Group %-10s: " ${D/\/sys\/fs\/cgroup/}; cat $D/cpuset.cpus;
	done

	echo -n "Current task running on: "
	cat /proc/self/cgroup

}

# Setup CGroups if required
setup() {

	# Check for required ROOT permissions
	check_root

	echo "Setup CGroups..."
	if [ ! -f $EMV_CGMOUNT/tasks ]; then
		echo "Mounting CGroups on [$EMV_CGMOUNT]..."
		mkdir -p $EMV_CGMOUNT >/dev/null 2>&1
		check_controllers
	fi

	echo "Setup CPUs shiled for $EMV_TRGT cluster..."
	shield_build
}

do_experiments() {

# Move ourself on target cluster
echo "Switching to $EMV_TRGT cluster"
echo $$ > $EMV_CGMOUNT/$EMV_TRGT/tasks

dump_conf

# Experiments to run
for E in $EMV_EXPS; do

	# Duration of each experiment
	for D in $EMV_DRTS; do

		case $E in
		1)
			WORKLOAD="./wlg -d$D -b3"
			;;
		2)
			WORKLOAD="./wlg -d$D -i3,100000,10000,200000,20000,300000,30000"
			;;
		3)
			WORKLOAD="./wlg -d$D -p3,100000,20,200000,40,300000,80"
			;;
		4)
			WORKLOAD="./wlg -d$D -b1 -p2,300000,30,10000,30"
			;;
		5)
			WORKLOAD="htop"
			;;
		esac

		echo "Testing [$WORKLOAD]..."
		[ "x$WORKLOAD" != "x" ] && \
			./idlestat -e energy_model_TC2 --trace -f trace.dat -t60 -- $WORKLOAD

	done
done

# Move ourself back to host cluster
echo "Switching back to $EMV_HOST cluster"
echo $$ > $EMV_CGMOUNT/$EMV_HOST/cgroup.procs

}

do_plot() {

cat >/tmp/tc2plots.gplot <<EOF
set terminal pngcairo enhanced font "arial,10" size 1920, 1080

set ylabel "Measured"
set xlabel "Estimated"

set output 'Idlestat_EM_Correlation.png'

# Setup subplots
set multiplot layout 1, 3 title "VExpress TC2 - Energy Model Correlation Analysis"
set tmargin 2

set ylabel  'Estimated'
set xlabel  'Measured'

set title "ClusterA (2xA15)"
plot 'tc2_energy.dat' using 5:4 with points ps 2

set title "ClusterB (3xA7)"
plot 'tc2_energy.dat' using 10:9 with points ps 2

set title "Total"
plot 'tc2_energy.dat' using 12:11 with points ps 2

EOF

gnuplot /tmp/tc2plots.gplot

}

run() {
	setup
	do_experiments
	shield_release
}

plot() {
	do_plot
}

case "$1" in
	run)
		run
		;;
	plot)
		plot
		;;
	setup)
		setup
		;;
	release)
		shield_release
		;;
	*)
		echo "Usage: $0 {run|plot}, or {setup|release} for cgroups management only"
		exit 1
		;;
esac

# vim: set tabstop=4:
