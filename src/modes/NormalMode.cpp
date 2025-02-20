#include "NormalMode.hpp"
#include <cassert>
#include <utility>
#include <iostream>
#include "../utils/utils.hpp"
#include "../utils/FileIO.hpp"
#include "../arguments/measureSelector.hpp"
#include "../arguments/MethodSelector.hpp"
#include "../arguments/GraphLoader.hpp"
#include "../Report.hpp"

#include <future>
#include <limits>

void NormalMode::run(ArgumentParser& args) {
    createFolders();

    //before loading graphs, check that the user did not forget to provide the execution time/iter
    //this is just to detect this common mistake early
    if (args.strings["-method"] == "sana") MethodSelector::validateRunTimeSpec(args);

    pair<Graph, Graph> graphs = GraphLoader::initGraphs(args);
    Graph G1 = graphs.first;
    Graph G2 = graphs.second;

    MeasureCombination M;
    measureSelector::initMeasures(M, G1, G2, args); 
    // TODO: use future and async to create a bunch of alignments simultaneously (see arguments/graphLoader for an example)
    // something like this:
    // auto futureA[number]; for(i=0; i<number; i++) futureA[i] = async(method->runAndPrintTime().....); A[i].get();
    // and then either: pick the best one and output only that... OR
    // go through all of them and produce a "Network Alignment Frequency" for all pairs of aligned nodes.

    int totalAlign = args.doubles["-parallelTotalAlign"];
    int simul = args.doubles["-parallelBatch"]; 
    int reportAll = args.doubles["-parallelReportAll"];

    if (totalAlign < simul) { throw runtime_error("totalAlign should be greater or equal to simulAlign."); }

    Method* methodList[totalAlign];
    std::future<Alignment> futureList[totalAlign];
    Alignment alignmentList[totalAlign];

    int futureIndex = 0;
    int alignIndex = 0;
    int length = totalAlign;

    while (totalAlign > 0) {
        int iterTime = totalAlign >= simul ? simul : totalAlign;

        for (; futureIndex < alignIndex + iterTime; futureIndex++) {
            methodList[futureIndex] = MethodSelector::initMethod(G1, G2, args, M);
            futureList[futureIndex] = std::async(std::launch::async, &Method::runAndPrintTime, methodList[futureIndex]);
        }

        for (; alignIndex < futureIndex; alignIndex++) {
            alignmentList[alignIndex] = futureList[alignIndex].get();
        }

        totalAlign -= iterTime;
    }

    double bestScore = -1 * numeric_limits<double>::infinity();
    Alignment A;
    Method* method;

    for (int i = 0; i < length; i++) {
        if (M.eval(alignmentList[i]) > bestScore) {
            A = alignmentList[i];
            method = methodList[i];
            bestScore = M.eval(alignmentList[i]);
        }
    }
    
    A.printDefinitionErrors(G1,G2);
    assert(A.isCorrectlyDefined(G1, G2) and "Resulting alignment is not correctly defined");

    string fileName = args.strings["-o"];
    bool multi_iter_only = (args.bools["-multi-iteration-only"] ? true : false);

    if (reportAll == 0) {
        Report::saveReport(G1, G2, A, M, method, fileName, !multi_iter_only);
        Report::saveLocalMeasures(G1, G2, A, M, method, args.strings["-localScoresFile"]);
    } else {
        fileName = Report::getFileName(fileName, G1, G2, method, A, "out");
        string localMeasureFileName = Report::getFileName(args.strings["-localScoresFile"], G1, G2, method, A, "localscores");
        string baseName = FileIO::fileNameWithoutExtension(fileName);
        ofstream alignOfs(baseName + ".align");
        ofstream outOfs(fileName);
        ofstream localMeasureOfs(localMeasureFileName);

        if (!multi_iter_only) {
            Graph CS = G1.graphIntersection(G2, *(A.getVector()));
            string aligGraphFileName = baseName + ".ccs-el";
            cout << "Saving common subgraph in edge list format as \"" << aligGraphFileName << "\"" << endl;
            GraphLoader::saveInEdgeListFormat(CS, aligGraphFileName, false, true, "", " ");
        }

        for (int i = 0; i < length; i++) {
            Report::reportAll(G1, G2, alignmentList[i], M, methodList[i], baseName, alignOfs, outOfs, !multi_iter_only, i + 1);
            Report::saveAllLocalMeasures(G1, G2, A, M, method, localMeasureOfs, args.strings["-localScoresFile"], i + 1);
        }
    }

    for (int i = 0; i < simul; i++) {
        delete methodList[i];
    }
}

string NormalMode::getName() { return "NormalMode"; }

void NormalMode::createFolders() {
    FileIO::createFolder("matrices"); //are all these still used? -Nil
    FileIO::createFolder("tmp");
    FileIO::createFolder("alignments");
    FileIO::createFolder("go");
    FileIO::createFolder("go/autogenerated");
    FileIO::createFolder("networks");
    FileIO::createFolder(AUTOGENEREATED_FILES_FOLDER);
}

