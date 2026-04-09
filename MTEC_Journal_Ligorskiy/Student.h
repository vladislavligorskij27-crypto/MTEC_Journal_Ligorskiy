#pragma once
#include <string>
#include <vector>
#include <map>

struct GradeRecord {
    std::string value; 

    int type = 0;      

    std::string date; 
};

class Student {
public:
    std::string fullName;
    std::string group;
    std::string specialty;

    std::map<int, std::map<std::string, std::vector<GradeRecord>>> semesterGrades;

    std::map<std::string, std::vector<GradeRecord>> archiveGrades;

    bool parseGradeForMath(const GradeRecord& g, int& outVal, int& outWeight) const {
        if (g.value == "Н" || g.value == "У" || g.value == "Б") return false;

        try {
            outVal = std::stoi(g.value);
            outWeight = (g.type == 2) ? 2 : 1; 
            return true;
        }
        catch (...) {
            return false;
        }
    }

    double getSubjectAverage(int semester, const std::string& subject) const {
        if (semesterGrades.count(semester) == 0) return 0.0;

        auto it = semesterGrades.at(semester).find(subject);
        if (it == semesterGrades.at(semester).end() || it->second.empty()) {
            return 0.0;
        }

        double sum = 0;
        int totalWeight = 0;

        for (const auto& g : it->second) {
            int val = 0, weight = 0;
            if (parseGradeForMath(g, val, weight)) {
                sum += (val * weight);
                totalWeight += weight;
            }
        }
        return totalWeight == 0 ? 0.0 : sum / totalWeight;
    }

    double getTotalAverage(int semester) const {
        if (semesterGrades.count(semester) == 0) return 0.0;

        double totalSum = 0;
        int totalWeight = 0;

        for (const auto& pair : semesterGrades.at(semester)) {
            for (const auto& g : pair.second) {
                int val = 0, weight = 0;
                if (parseGradeForMath(g, val, weight)) {
                    totalSum += (val * weight);
                    totalWeight += weight;
                }
            }
        }

        return totalWeight == 0 ? 0.0 : totalSum / totalWeight;
    }
};