#!/usr/bin/env python
import rospy
from geometry_msgs.msg import WrenchStamped

factor = 30
pub = rospy.Publisher('dynpick/multiplied_force', WrenchStamped, queue_size = 1)

def callback(data):
        data.wrench.force.x = data.wrench.force.x * factor
        data.wrench.force.y = data.wrench.force.y * factor
        data.wrench.force.z = data.wrench.force.z * factor
        pub.publish(data)

def listener():
        rospy.init_node('wrench_listener', anonymous=True)
        rospy.Subscriber("dynpick/force", WrenchStamped, callback)
        
        rospy.spin()

if __name__ == '__main__':
        listener()