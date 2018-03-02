// Example code @ http://wiki.ros.org/pcl/Tutorials

#include <ros/ros.h>
#include <ros/console.h>
#include <visualization_msgs/Marker.h>
// PCL specific includes
#include <sensor_msgs/PointCloud2.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/filters/voxel_grid.h>
#include <vector>
#include <cmath>

struct Point
{
	double _x, _y;
};

// Struct for lines
// Lines stored as y = mx + b, _slope = m, _yInt = b
// struct stores min/max x and y values of points in set, as well 
// as outlier point and its distance R to the line

struct Line 
{
	double _slope, _yInt, xmin, xmax, xmean, ymin, ymax, out_x, out_y, R;
	bool vline;
	double getYforX(double x) {
		if(vline)
		{
			ROS_DEBUG("Called getYforX for vline");
		}
		else
		{
			return _slope*x + _yInt;
		}	
	}
  // Construct line from points
	bool fitPoints(std::vector<Point> & points) 
	{
		int nPoints = points.size();
		if( nPoints < 2 ) 
		{
        // Infinite lines fit a single point 
			return false;
		}
		double sumX=0, sumY=0, sumXY=0, sumX2=0, tempXmin = 1000, tempXmax = -1000, tempYmin = 1000, tempYmax = -1000;
		for(int i=0; i<nPoints; i++) 
		{
			// Find min/max x and y coords
			if (points[i]._x < tempXmin)
				tempXmin = points[i]._x;
			if (points[i]._x > tempXmax)
				tempXmax = points[i]._x;
			if (points[i]._y < tempYmin)
				tempYmin = points[i]._y;
			if (points[i]._y > tempYmax)
				tempYmax = points[i]._y;

			// Calc sum and sum of squares
			sumX += points[i]._x;
			sumY += points[i]._y;
			sumXY += points[i]._x * points[i]._y;
			sumX2 += points[i]._x * points[i]._x;
		}
		// Set range of vals
		xmin = tempXmin;
		xmax = tempXmax;
		ymin = tempYmin;
		ymax = tempYmax;

		// prep for finding LSRL
		xmean = sumX / nPoints;
		double yMean = sumY / nPoints;
		double denominator = sumX2 - sumX * xmean;
		double delta, temp = 0;

		// Check for vertical line
		if( std::fabs(denominator) < 1e-7 ) 
		{
			vline = true;
	    	// Find outlier
			for(int i=0; i<nPoints; i++) 
			{
				delta = points[i]._x - xmean;
				if(std::fabs(delta) > std::fabs(temp))
				{
					temp = delta;
					out_x = points[i]._x;
					out_y = points[i]._y; 
				}
			}
			R = temp;
			ROS_DEBUG("Found line: x = %f , R val: %f ", xmean,R);
		}
		else 
		{
			vline = false;
			_slope = (sumXY - sumX * yMean) / denominator;
			_yInt = yMean - _slope * xmean;
			// Find outlier
			for(int i=0; i<nPoints; i++) 
			{
				// distance from point to line using dot product
				delta = std::fabs(points[i]._y - (points[i]._x * _slope) - _yInt) / sqrt(1 + _slope*_slope);
				if(std::fabs(delta) > std::fabs(temp))
				{
					temp = delta;
					out_x = points[i]._x;
					out_y = points[i]._y;
				}
			}
			R = temp;
			ROS_DEBUG("Found line: y = %fx + %f , R val: %f ", _slope,_yInt,R);
		}
		return true;
	}
};

class cloud_parse
{
public:
	cloud_parse()
	{
  	// Create a ROS subscriber for the input point cloud
		sub = nh.subscribe ("/cloud_data", 100, &cloud_parse::cloud_cb, this);

  // Create a ROS publisher for the extracted lines
		marker_pub = nh.advertise<visualization_msgs::Marker>("/visualization_marker", 10);
		ROS_DEBUG("Initialized cloud_parse object...");
	}

  // Callback function upon recieving point cloud
	void cloud_cb (const sensor_msgs::PointCloud2ConstPtr& cloud_msg)
	{
		ROS_DEBUG("Running cb for recieved point cloud.");
		std::vector<Point> points;
		points.clear();
		std::vector<Line> lines;
		lines.clear();
		Point temp;
  // Container for original & filtered data
		pcl::PCLPointCloud2* cloud = new pcl::PCLPointCloud2; 
		pcl::PCLPointCloud2ConstPtr cloudPtr(cloud);
		pcl::PCLPointCloud2 cloud_filtered;

  // Convert to PCL data type
		pcl_conversions::toPCL(*cloud_msg, *cloud);

  // Reduce point cloud size and remove redundant points
		pcl::VoxelGrid<pcl::PCLPointCloud2> sor;
		sor.setInputCloud (cloudPtr);
		sor.setLeafSize (0.1, 0.1, 0.1);
		sor.filter (cloud_filtered);

  // Convert to ROS data type
		sensor_msgs::PointCloud2 output;
		pcl_conversions::fromPCL(cloud_filtered, output);

	// Extract points from pointcloud 
		pcl::PointCloud<pcl::PointXYZ> msg_;
		pcl::fromROSMsg(output,msg_);

	// Set ground reference point
		float ground = 0.3;

	// Filter points by z-coord and add to points vector
		int nPoints = msg_.points.size();
		for(int i=0; i<nPoints; i++) 
		{
			if(msg_.points[i].z >= ground)
			{ 
				temp._x = msg_.points[i].x;
				temp._y = msg_.points[i].y;
				points.push_back(temp);
				ROS_DEBUG("Adding point (%f,%f) to set S. ",temp._x,temp._y);
			}
		}

  	// Split and merge line extraction
		lines = split_and_merge(points);

	// Publish and display lines
		visualization_msgs::Marker line_strip;
		line_strip.header.frame_id = "/velodyne";
		line_strip.header.stamp = ros::Time::now();
		line_strip.ns = "lines";
		line_strip.action = visualization_msgs::Marker::ADD;
		line_strip.pose.orientation.w = 1.0;
		line_strip.id = 0;
		line_strip.type = visualization_msgs::Marker::LINE_STRIP;

    // LINE_STRIP/LINE_LIST markers use only the x component of scale, for the line width
		line_strip.scale.x = 0.3;

    // Line strip is blue
		line_strip.color.b = 1.0;
		line_strip.color.a = 1.0;

    // Create the vertices for the points and lines
		int nLines = lines.size();
		ROS_DEBUG("Publishing %i lines.", nLines);
		double x,y;
		for (int i=0;i<nLines;i++)
		{
			if (lines[i].vline)
			{
				for (y = lines[i].ymin; y < lines[i].ymax;y+=(lines[i].ymax-lines[i].ymin)/25)
				{
					
					geometry_msgs::Point p;
					p.x = lines[i].xmean;;
					p.y = y;
					p.z = 0.5;

					line_strip.points.push_back(p);
				}
			}
			else
			{
				for (x = lines[i].xmin; x < lines[i].xmax;x+=(lines[i].xmax-lines[i].xmin)/25)
				{
					y = lines[i].getYforX(x);

					geometry_msgs::Point p;
					p.x = x;
					p.y = y;
					p.z = 0.5;

					line_strip.points.push_back(p);
				}
			}
		}
		marker_pub.publish(line_strip);
		line_strip.points.clear();
	}

	// Function runs split and merge algorithm on set of points to find all lines within that set
	std::vector<Line> split_and_merge(std::vector<Point> S)
	{
		ROS_DEBUG("Running split and merge on set S:");
		std::vector<Point> temp_p1, temp_p2, temp_p3;
		std::vector< std::vector<Point> > points;
		std::vector<Line> lines;
		Line line;
		int nSets, nPts;
		double threshold = 1e-3;
		bool split_and_extract = true, merged;
		points.push_back(S);

		while (split_and_extract)
		{
			lines.clear();
			nSets = points.size();
		// For every set try to extract line and check threshold
			for (int i=0;i<nSets;i++)
			{
			//fit line to points
				if (line.fitPoints(points[i]) != true)
				{
					// Remove singular set and do not fit line to it
					ROS_DEBUG("Attempted to fit line to single point.");
					points.erase(points.begin() + i);
				}
				else
				{
					lines.push_back(line);
				}
			}

			nSets = lines.size();
		// For every line check threshold
			bool split = false;
			for (int i=0;i<nSets;i++)
			{
			// if greatest outlier is bigger than threshold distance from line, split set 
				if (lines[i].R > threshold)
				{
					ROS_DEBUG("Splitting set S[%i] by point (%f,%f)",i,lines[i].out_x,lines[i].out_y);
					split = true;
					nPts = points[i].size();

				// split into two sets by x-coordinates or y-coodinates
					if (lines[i].vline)
					{
						for (int j=0;j<nPts;j++)
						{
							if (points[i][j]._y < lines[i].out_y)
							{
								ROS_DEBUG("Adding point (%f,%f) to temp set S1",points[i][j]._x, points[i][j]._y);
								temp_p1.push_back(points[i][j]);
							}
							else if (points[i][j]._y > lines[i].out_y)
							{
								ROS_DEBUG("Adding point (%f,%f) to temp set S2",points[i][j]._x, points[i][j]._y);
								temp_p2.push_back(points[i][j]);
							}
						}
					}
					else
					{
						for (int j=0;j<nPts;j++)
						{
							if (points[i][j]._x < lines[i].out_x)
							{
								ROS_DEBUG("Adding point (%f,%f) to set S1",points[i][j]._y, points[i][j]._x);
								temp_p1.push_back(points[i][j]);
							}
							else if (points[i][j]._x > lines[i].out_x)
							{
								ROS_DEBUG("Adding point (%f,%f) to set S2",points[i][j]._y, points[i][j]._x);
								temp_p2.push_back(points[i][j]);
							}
						}
					}
				// After splitting into two sets, re-run split_and_extract on all sets of points
					points.erase(points.begin() + i);

				// Only add sets which are not singular in size
					if (temp_p1.size() > 1)
					{
						points.push_back(temp_p1);
					} 
					else
					{
						ROS_DEBUG("Reducing set by 1 point");
					}

					if (temp_p2.size() > 1)
					{
						points.push_back(temp_p2);
					} 
					else
					{
						ROS_DEBUG("Reducing set by 1 point");
					}
						

					ROS_DEBUG("Split into two sets of size S1:%lu, S2:%lu",temp_p1.size(),temp_p2.size());
					temp_p1.clear();
					temp_p2.clear();
				}
			}

		// If we had a split, re-run loop and clear set of lines
			if (split == false)
			{
				split_and_extract = false;
			}
		}

	// Attempt merging co-linear lines 
		for (int i=0;i<(lines.size()-1);i++)
		{
			merged = false;
		// check if lines co-linear with rest of lines in set 
			for (int j=i+1;j<lines.size();j++)
			{
				if(merged)
				{
					i--;
					break;
				}
			// if co-linear check fit, otherwise change nothing
				if(lines[i]._slope-lines[j]._slope < 1e-2 || lines[i].vline && lines[j].vline)
				{
					ROS_DEBUG("Checking if co-linear lines can be merged");
					temp_p3.clear();
				// First add all points into temp set 3
					for(int k=0;k<points[i].size();k++)
					{
						temp_p3.push_back(points[i][k]);
					}
					for(int k=0;k<points[j].size();k++)
					{
						temp_p3.push_back(points[j][k]);
					}

				// check R value of line fit to see if we are merging
					if (line.fitPoints(temp_p3) == true)
					{
					// if line works remove old lines and sets of points, add new line and set
						if(line.R < threshold)
						{
							ROS_DEBUG("From set of %lu lines, merging lines %i and %i.",lines.size(),i,j);
							lines.erase(lines.begin() + i);
							points.erase(points.begin() + i);
							lines.erase(lines.begin() + j-1);
							points.erase(points.begin() + j-1);
							points.push_back(temp_p3);
							lines.push_back(line);
							merged = true;
						}
					}
				}
			}

		}
		nSets = lines.size();
		ROS_INFO("Found %i lines to fit PointCloud. ",nSets);
		return lines;
	}

private:
	ros::Publisher marker_pub;
	ros::Subscriber sub;
	ros::NodeHandle nh; 

};//End of class cloud_cb_pub

int main (int argc, char** argv)
{
    // Initialize ROS
	ros::init (argc, argv, "spm_node");
	cloud_parse cloud_parse_obj;

	// Spinning handles ROS events
	ros::spin();

	
	// ros::Rate r(100);
	// while (ros::ok())
	// {
	// 	//libusb_handle_events_timeout(...); // Handle USB events
	// 	ROS_DEBUG("Spinning...");
	// 	ros::spinOnce();                   // Handle ROS events
	// 	r.sleep();
	// }
	
	return 0;
}