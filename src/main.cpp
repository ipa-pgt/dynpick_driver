#include <ros/ros.h>
#include <geometry_msgs/WrenchStamped.h>
#include <std_srvs/Trigger.h>

#include	<stdio.h>
#include	<fcntl.h>
#include	<time.h>
#include	<termios.h>
#include	<string.h>
#include	<unistd.h>

#include <mutex>
#include <condition_variable>

#define true                1
#define false               0

// TODO: Find out why and fix the multiple try approach
#define RESET_COMMAND_TRY   3       // It only works when send several times.

std::mutex m_;
std::condition_variable cv_;
int offset_reset_ = 0;

int SetComAttr(int fdc)
	{
	int			n;

	struct termios	term;


	// Set baud rate
	n = tcgetattr(fdc, &term);
	if (n < 0)
		goto over;

	bzero(&term, sizeof(term));

	term.c_cflag = B921600 | CS8 | CLOCAL | CREAD;
	term.c_iflag = IGNPAR;
	term.c_oflag = 0;
	term.c_lflag = 0;/*ICANON;*/
 
	term.c_cc[VINTR]    = 0;     /* Ctrl-c */ 
	term.c_cc[VQUIT]    = 0;     /* Ctrl-? */
	term.c_cc[VERASE]   = 0;     /* del */
	term.c_cc[VKILL]    = 0;     /* @ */
	term.c_cc[VEOF]     = 4;     /* Ctrl-d */
	term.c_cc[VTIME]    = 0;
	term.c_cc[VMIN]     = 0;
	term.c_cc[VSWTC]    = 0;     /* '?0' */
	term.c_cc[VSTART]   = 0;     /* Ctrl-q */ 
	term.c_cc[VSTOP]    = 0;     /* Ctrl-s */
	term.c_cc[VSUSP]    = 0;     /* Ctrl-z */
	term.c_cc[VEOL]     = 0;     /* '?0' */
	term.c_cc[VREPRINT] = 0;     /* Ctrl-r */
	term.c_cc[VDISCARD] = 0;     /* Ctrl-u */
	term.c_cc[VWERASE]  = 0;     /* Ctrl-w */
	term.c_cc[VLNEXT]   = 0;     /* Ctrl-v */
	term.c_cc[VEOL2]    = 0;     /* '?0' */

//	tcflush(fdc, TCIFLUSH);
	n = tcsetattr(fdc, TCSANOW, &term);
over :
	return (n);
	}

bool offsetRequest(std_srvs::Trigger::Request  &req,
         std_srvs::Trigger::Response &res) {
    std::unique_lock<std::mutex> lock(m_);
    offset_reset_ = RESET_COMMAND_TRY;
    while (offset_reset_ > 0) {cv_.wait(lock);}
    lock.unlock();
    res.message = "Reset offset command was send " + std::to_string(RESET_COMMAND_TRY) + " times to the sensor.";
    res.success = true;
    return true;
}

int main(int argc, char **argv) {
    int fdc;
    int clock = 0;
    double rate;
    std::string devname, frame_id;

    fdc = -1;

    ros::init(argc, argv, "dynpick_driver");
    ros::NodeHandle n, nh("~");
    nh.param<std::string>("device", devname, "/dev/ttyUSB0");
    nh.param<std::string>("frame_id", frame_id, "/sensor");
    nh.param<double>("rate", rate, 1000);

    ros::ServiceServer service = n.advertiseService("tare", offsetRequest);
    ros::Publisher pub = n.advertise<geometry_msgs::WrenchStamped>("force", 1000);

    ros::AsyncSpinner spinner(2); // Use 2 threads
    spinner.start();

    // Open COM port
    ROS_INFO("Open %s", devname.c_str());

    for (int cnt = 0; cnt < 10; cnt++) {
        ROS_INFO("Opening device..");
        fdc = open(devname.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fdc < 0) {
            ROS_ERROR("could not open %s\n", devname.c_str());
            if (cnt >= 9) {
                return -1;
            } else {
                ros::Duration(0.5).sleep();
            }
        }
    }

    // Obtain sampling rate
    ROS_INFO("Sampling time = %f ms\n", 1.0/rate);

    // Set baud rate of COM port
    SetComAttr(fdc);

    // Request for initial single data
    write(fdc, "R", 1);

    ros::Rate loop_rate(rate);
    while (ros::ok()) {
        int c, len;
        char str[256];
        int tick;
        unsigned short data[6];

        geometry_msgs::WrenchStamped msg;

        std::unique_lock<std::mutex> lock(m_);
        if (offset_reset_ <= 0){
            // Request for initial data (2nd round)
            write(fdc, "R", 1);

            // Obtain single data
    #define DATA_LENGTH 27
            len = 0;
            while ( len < DATA_LENGTH ) {
                    c = read(fdc, str+len, DATA_LENGTH-len);
                    if (c > 0) {
                        len += c;
                    } else {
                        ROS_DEBUG("=== need to read more data ... n = %d (%d) ===", c, len);
                        continue;
                    }
                }
            if ( len != DATA_LENGTH ) {
                ROS_WARN("=== error reciving data ... n = %d ===", c);
            }

            sscanf(str, "%1d%4hx%4hx%4hx%4hx%4hx%4hx",
                   &tick, &data[0], &data[1], &data[2], &data[3], &data[4], &data[5]);

            msg.header.frame_id = frame_id;
            msg.header.stamp = ros::Time::now();
            msg.header.seq = clock++;

            msg.wrench.force.x = (data[0]-8192)/1000.0;
            msg.wrench.force.y = (data[1]-8192)/1000.0;
            msg.wrench.force.z = (data[2]-8192)/1000.0;
            msg.wrench.torque.x = (data[3]-8192)/1000.0;
            msg.wrench.torque.y = (data[4]-8192)/1000.0;
            msg.wrench.torque.z = (data[5]-8192)/1000.0;

            pub.publish(msg);
        } else {
            // Request for offset reset
            write(fdc, "O", 1);
            offset_reset_ --;
            cv_.notify_all();
        }
        lock.unlock();
        ros::spinOnce();

        loop_rate.sleep();
    }

    return 0;
}
