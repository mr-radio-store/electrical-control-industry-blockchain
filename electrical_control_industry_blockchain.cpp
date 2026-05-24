#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <random>
#include <openssl/sha.h>
#include <cmath>

using namespace std;
using namespace cv;

/* ================= SHA256 ================= */
string sha256(const string &input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)input.c_str(), input.size(), hash);
    stringstream ss;
    for(int i=0;i<SHA256_DIGEST_LENGTH;i++)
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    return ss.str();
}

/* ================= REGIONAL POWER DATA ================= */
struct RegionPower {
    string regionName;
    double demandMW;
    double supplyMW;
    double price;        // dynamic price based on imbalance
    string status;       // "Normal", "Overload", "Outage"
    string signature;
};

/* ================= SIGNATURE ================= */
string privateKey = "grid_key";
string publicKey  = "grid_key";

string signData(const RegionPower &r) {
    string data = r.regionName + to_string(r.demandMW) + to_string(r.supplyMW) + r.status;
    return sha256(data + privateKey);
}

bool verifySignature(const RegionPower &r) {
    string data = r.regionName + to_string(r.demandMW) + to_string(r.supplyMW) + r.status;
    return sha256(data + publicKey) == r.signature;
}

/* ================= BLOCK ================= */
class Block {
public:
    int index;
    time_t timestamp;
    vector<RegionPower> data; // all regions in one block
    string previousHash;
    string hash;

    Block(int idx, const vector<RegionPower> &regions, string prev) {
        index = idx;
        timestamp = time(nullptr);
        data = regions;
        previousHash = prev;
        hash = calculateHash();
    }

    string calculateHash() const {
        stringstream ss;
        ss << index << timestamp << previousHash;
        for(const auto &r : data)
            ss << r.regionName << r.demandMW << r.supplyMW << r.status;
        return sha256(ss.str());
    }
};

/* ================= BLOCKCHAIN ================= */
class Blockchain {
public:
    vector<Block> chain;

    Blockchain() {
        vector<RegionPower> genesis = {{"GENESIS",0,0,0,"",""}};
        chain.push_back(Block(0, genesis, "0"));
    }

    void addBlock(const vector<RegionPower> &regions) {
        // verify all signatures
        for(const auto &r : regions) {
            if(!verifySignature(r)) return;
        }
        chain.push_back(Block(chain.size(), regions, chain.back().hash));
    }

    bool isValid() {
        for(size_t i=1;i<chain.size();i++) {
            if(chain[i].hash != chain[i].calculateHash()) return false;
            if(chain[i].previousHash != chain[i-1].hash) return false;
        }
        return true;
    }
};

/* ================= VISUALIZATION ================= */
void drawFrame(Mat &frame, const vector<RegionPower> &regions) {
    frame = Mat::zeros(600,1000,CV_8UC3);
    putText(frame,"Smart Grid Power Simulation",
            Point(200,50), FONT_HERSHEY_SIMPLEX,0.9, Scalar(0,255,255),2);

    int barWidth = 40;
    int spacing = 150;
    int startX = 100;

    for(size_t i=0;i<regions.size();i++) {
        int x = startX + i*spacing;
        int demandHeight = (int)(regions[i].demandMW*4);
        int supplyHeight = (int)(regions[i].supplyMW*4);

        // Demand bar
        rectangle(frame, Rect(x,500-demandHeight, barWidth, demandHeight), Scalar(0,0,255), -1);
        // Supply bar
        rectangle(frame, Rect(x+barWidth+10,500-supplyHeight, barWidth, supplyHeight), Scalar(0,255,0), -1);

        // Region label
        putText(frame, regions[i].regionName, Point(x,520), FONT_HERSHEY_SIMPLEX,0.6,Scalar(255,255,255),1);

        // Status
        Scalar statusColor = (regions[i].status=="Overload") ? Scalar(0,0,255) :
                             (regions[i].status=="Outage") ? Scalar(0,165,255) : Scalar(0,255,0);
        putText(frame, regions[i].status, Point(x,50), FONT_HERSHEY_SIMPLEX,0.7,statusColor,2);

        // Price label
        putText(frame,"$"+to_string(int(regions[i].price)), Point(x,480-demandHeight),
                FONT_HERSHEY_SIMPLEX,0.5, Scalar(255,255,0),1);
    }
}

/* ================= MAIN ================= */
int main() {
    Blockchain chain;
    VideoWriter video("power_simulation.mp4", VideoWriter::fourcc('m','p','4','v'),30, Size(1000,600));
    Mat frame;

    random_device rd;
    mt19937 gen(rd());
    uniform_real_distribution<> demandFluct(20.0, 50.0);
    uniform_real_distribution<> supplyFluct(50.0, 100.0);
    uniform_real_distribution<> failDist(0.0,1.0);

    vector<string> regionNames = {"North","South","East","West"};

    for(int step=0; step<150; step++) {
        vector<RegionPower> regions;

        // Simulate time of day (0-23 hours)
        double hour = step % 24;
        double peakFactor = 1.0 + 0.5*sin((hour-6)*M_PI/12.0); // morning and evening peaks

        for(auto &rname : regionNames) {
            double baseDemand = 30.0 + demandFluct(gen);
            double demand = baseDemand * peakFactor;

            // Supply correlated to region
            double supply = supplyFluct(gen);
            if(rname=="South" || rname=="East") {
                double solar = 50 + 30*sin(M_PI*(hour)/12.0); // solar peak at noon
                supply = solar + supplyFluct(gen)/2;
            }

            string status = (demand > supply) ? "Overload" : "Normal";

            // Random outages
            if(failDist(gen) < 0.03) {
                supply = 0;
                status = "Outage";
            }

            double price = 0.1 * demand/supply;
            if(status=="Overload") price *= 2;

            RegionPower r = {rname, demand, supply, price, status, ""};
            r.signature = signData(r);

            regions.push_back(r);
        }

        chain.addBlock(regions);
        drawFrame(frame, regions);
        video.write(frame);
    }

    video.release();
    imwrite("power_result.jpg", frame);

    cout<<"Blockchain valid: "<<chain.isValid()<<endl;
    cout<<"Total blocks: "<<chain.chain.size()<<endl;
}