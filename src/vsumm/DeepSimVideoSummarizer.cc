/*
    Copyright (C) Rishabh Iyer
    Author: Rishabh Iyer
    Simple Video Summarizer using Color Histogram
 *
 */
#include "DeepSimVideoSummarizer.h"

static std::string IntToString(int a)
{
    stringstream ss;
    ss << a;
    string str = ss.str();
    return str;
}

float DotProduct(std::vector<float> vec1, std::vector<float> vec2)
{
    if (vec1.size() != vec2.size())
    {
        std::cout << "Error: Both vectors need to be of the same size\n";
    }
    float sim = 0;
    float norm1 = 0;
    float norm2 = 0;
    int n = vec1.size();
    for (int i = 0; i < n; i++)
    {
        norm1 += vec1[i]*vec1[i];
        norm2 += vec2[i]*vec2[i];
        for (int j = 0; j < n; j++)
        {
            sim += vec1[i]*vec2[j];
        }
    }
    return sim/(sqrt(norm1)*sqrt(norm2));
}

float GaussianSimilarity(std::vector<float> vec1, std::vector<float> vec2)
{
    if (vec1.size() != vec2.size())
    {
        std::cout << "Error: Both vectors need to be of the same size\n";
    }
    float diff = 0;
    float norm1 = 0;
    float norm2 = 0;
    int n = vec1.size();
    for (int i = 0; i < n; i++)
    {
        norm1 += vec1[i]*vec1[i];
        norm2 += vec2[i]*vec2[i];
    }
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < vec2.size(); j++)
        {
            diff += vec1[i]/sqrt(norm1) - vec2[j]/sqrt(norm2);
        }
    }
    return exp(-diff/2);
}

DeepSimVideoSummarizer::DeepSimVideoSummarizer(char* videoFile, CaffeClassifier& cc, std::string featureLayer, int summaryFunction, int segmentType,
  int snippetLength, bool debugMode) : videoFile(videoFile), cc(cc), featureLayer(featureLayer), summaryFunction(summaryFunction), segmentType(segmentType),
  snippetLength(snippetLength), debugMode(debugMode)
{
    cv::VideoCapture capture(videoFile);

    frameRate = (int) capture.get(CV_CAP_PROP_FPS);
    videoLength = capture.get(CV_CAP_PROP_FRAME_COUNT)/frameRate;
    std::cout << "The video Length is " << videoLength << " and the frameRate is " << frameRate << "\n";
    if (segmentType == 0) {
      for (int i = 0; i < videoLength; i += snippetLength)
        segmentStartTimes.push_back(i);
    }
    else {
        segmentStartTimes = shotDetector(capture);
    }
    capture.release();
}

void DeepSimVideoSummarizer::extractFeatures(){
    cv::VideoCapture capture(videoFile);
    capture.set(CV_CAP_PROP_POS_FRAMES, 0);
    cv::Mat frame;
    std::vector<cv::Mat> CurrVideo = std::vector<cv::Mat> ();
    if( !capture.isOpened() )
            std::cout << "Error when reading steam" << "\n";
    int frame_count = 0;
    int samplingRate = 1;
    costList = std::vector<double>();
    for (int i = 0; i < segmentStartTimes.size()-1; i++)
    {
      if (segmentStartTimes[i+1] - segmentStartTimes[i] == 1) {
          cv::MatND hist;
          capture.set(CV_CAP_PROP_POS_FRAMES, segmentStartTimes[i]*frameRate);
          capture >> frame;
          std::vector<float> feat = cc.Predict(frame, featureLayer);
          classifierFeatures.push_back(feat);
          if (segmentType == 1)
              costList.push_back(SmallShotPenalty);
          else
              costList.push_back(1);
      }
      else {
          for (int j = segmentStartTimes[i]; j < segmentStartTimes[i+1]; j++)
          {
              capture.set(CV_CAP_PROP_POS_FRAMES, j*frameRate);
              capture >> frame;
              CurrVideo.push_back(frame.clone());
          }
          cv::MatND hist;
          std::vector<float> feat = cc.Predict(CurrVideo, featureLayer);
          classifierFeatures.push_back(feat);
          costList.push_back(CurrVideo.size());
          CurrVideo.clear();
      }
      if (debugMode)
      {
        std::vector<std::pair<std::string, float>> res = cc.Classify(frame);
        std::string labels = "";
        for (int i = 0; i < res.size() - 1; i++)
        {
            labels = labels + res[i].first + ", ";
        }
        labels = labels + res[res.size() - 1].first;
        cv::putText(frame, labels, cvPoint(30,30),
            cv::FONT_HERSHEY_COMPLEX_SMALL, 0.8, cvScalar(200,200,250), 1, CV_AA);
        if (frame.data)
            cv::imshow("Debug Video", frame);
            // Press  ESC on keyboard to exit
        char c = (char)cv::waitKey(25);
        if(c==27)
            break;
      }
    }
    capture.release();
    n = classifierFeatures.size();
    capture.release();
}

void DeepSimVideoSummarizer::computeKernel(int compare_method)
{
    // compare_method is the comparision method for similarity (0: DotProduct, 1:GaussianSimilarity)
    float max = 0;
    for (int i = 0; i < n; i++)
    {
        std::vector<float> currvector;
        for (int j = 0; j < n; j++)
        {
            float val;
            if (compare_method == 0)
                val = DotProduct(classifierFeatures[i], classifierFeatures[j]);
            else
                val = GaussianSimilarity(classifierFeatures[i], classifierFeatures[j]);
            currvector.push_back(val);
        }
        kernel.push_back(currvector);
    }
}

void DeepSimVideoSummarizer::summarizeBudget(int budget){
    Set optSet;
    if (summaryFunction == 0)
    {
      DisparityMin dM(n, kernel);
      int inititem = 1; //rand()%n;
      optSet.insert(inititem);
      naiveGreedyMaxKnapsack(dM, costList, budget, optSet, 1, false, true);
      summarySet = std::set<int>();
      for (Set::iterator it = optSet.begin(); it!=optSet.end(); it++)
      {
          summarySet.insert(*it);
      }
    }
    else if (summaryFunction == 1)
    {
      MMR m(n, kernel);
      int inititem = rand()%n;
      optSet.insert(inititem);
      naiveGreedyMaxKnapsack(m, costList, budget, optSet, 1, false, true);
      summarySet = std::set<int>();
      for (Set::iterator it = optSet.begin(); it!=optSet.end(); it++)
      {
          summarySet.insert(*it);
      }
    }
    else if (summaryFunction == 2)
    {
      FacilityLocation fL(n, kernel);
      lazyGreedyMaxKnapsack(fL, costList, budget, optSet, 1);
      summarySet = std::set<int>();
      for (Set::iterator it = optSet.begin(); it!=optSet.end(); it++)
      {
          summarySet.insert(*it);
      }
    }
    else if (summaryFunction == 3)
    {
      GraphCutFunctions gC(n, kernel, 0.5);
      lazyGreedyMaxKnapsack(gC, costList, budget, optSet, 1);
      summarySet = std::set<int>();
      for (Set::iterator it = optSet.begin(); it!=optSet.end(); it++)
      {
          summarySet.insert(*it);
      }
    }
    else if (summaryFunction == 4)
    {
      SaturateCoverage sC(n, kernel, 0.1);
      lazyGreedyMaxKnapsack(sC, costList, budget, optSet, 1);
      summarySet = std::set<int>();
      for (Set::iterator it = optSet.begin(); it!=optSet.end(); it++)
      {
          summarySet.insert(*it);
      }
    }
    // cout << "Done with summarization\n" << flush;
}

void DeepSimVideoSummarizer::summarizeStream(double epsilon){
    Set optSet;
    if (summaryFunction == 0)
    {
      DisparityMin dM(n, kernel);
      optSet.insert(0);
      vector<int> order(n, 1);
      for (int i = 0; i < n; i++)
          order[i] = i;
      streamGreedy(dM, epsilon, optSet, order);
      optSet.insert(n-1);
      summarySet = std::set<int>();
      for (Set::iterator it = optSet.begin(); it!=optSet.end(); it++)
      {
          summarySet.insert(*it);
      }
    }
    else if (summaryFunction == 1)
    {
      MMR m(n, kernel);
      optSet.insert(0);
      vector<int> order(n, 1);
      for (int i = 0; i < n; i++)
          order[i] = i;
      streamGreedy(m, epsilon, optSet, order);
      optSet.insert(n-1);
      summarySet = std::set<int>();
      for (Set::iterator it = optSet.begin(); it!=optSet.end(); it++)
      {
          summarySet.insert(*it);
      }
    }
    else if (summaryFunction == 2)
    {
      FacilityLocation fL(n, kernel);
      optSet.insert(0);
      vector<int> order(n, 1);
      for (int i = 0; i < n; i++)
          order[i] = i;
      streamGreedy(fL, epsilon, optSet, order);
      optSet.insert(n-1);
      summarySet = std::set<int>();
      for (Set::iterator it = optSet.begin(); it!=optSet.end(); it++)
      {
          summarySet.insert(*it);
      }
    }
    else if (summaryFunction == 3)
    {
      GraphCutFunctions gC(n, kernel, 0.5);
      optSet.insert(0);
      vector<int> order(n, 1);
      for (int i = 0; i < n; i++)
          order[i] = i;
      streamGreedy(gC, epsilon, optSet, order);
      optSet.insert(n-1);
      summarySet = std::set<int>();
      for (Set::iterator it = optSet.begin(); it!=optSet.end(); it++)
      {
          summarySet.insert(*it);
      }
    }
    else if (summaryFunction == 4)
    {
      SaturateCoverage sC(n, kernel, 0.1);
      optSet.insert(0);
      vector<int> order(n, 1);
      for (int i = 0; i < n; i++)
          order[i] = i;
      streamGreedy(sC, epsilon, optSet, order);
      optSet.insert(n-1);
      summarySet = std::set<int>();
      for (Set::iterator it = optSet.begin(); it!=optSet.end(); it++)
      {
          summarySet.insert(*it);
      }
    }
}

void DeepSimVideoSummarizer::summarizeCover(double coverage){
    Set optSet;
    if (summaryFunction == 0)
    {
      std::cout << "Cover Summarization is not supported for Disparity Min Function\n";
    }
    else if (summaryFunction == 1)
    {
      std::cout << "Cover Summarization is not supported for MMR Function\n";
    }
    else if (summaryFunction == 2)
    {
      FacilityLocation fL(n, kernel);
      lazyGreedyMaxSC(fL, costList, coverage, optSet, 0);
      summarySet = std::set<int>();
      for (Set::iterator it = optSet.begin(); it!=optSet.end(); it++)
      {
          summarySet.insert(*it);
      }
    }
    else if (summaryFunction == 3)
    {
      GraphCutFunctions gC(n, kernel, 0.5);
      lazyGreedyMaxSC(gC, costList, coverage, optSet, 0);
      summarySet = std::set<int>();
      for (Set::iterator it = optSet.begin(); it!=optSet.end(); it++)
      {
          summarySet.insert(*it);
      }
    }
    else if (summaryFunction == 4)
    {
      SaturateCoverage sC(n, kernel, 0.1);
      lazyGreedyMaxSC(sC, costList, coverage, optSet, 0);
      summarySet = std::set<int>();
      for (Set::iterator it = optSet.begin(); it!=optSet.end(); it++)
      {
          summarySet.insert(*it);
      }
    }
    // cout << "Done with summarization\n" << flush;
}

void DeepSimVideoSummarizer::playAndSaveSummaryVideo(char* videoFileSave)
{
    cv::VideoCapture capture(videoFile);
    cv::Mat frame;
    capture.set(CV_CAP_PROP_POS_FRAMES, 0);
    cv::VideoWriter videoWriter;
    if (videoFileSave != "")
      videoWriter = cv::VideoWriter(videoFileSave, CV_FOURCC('M','J','P','G'), (int) capture.get(CV_CAP_PROP_FPS),
        cv::Size(capture.get(cv::CAP_PROP_FRAME_WIDTH),capture.get(cv::CAP_PROP_FRAME_HEIGHT)));
    for (std::set<int>::iterator it = summarySet.begin(); it != summarySet.end(); it++)
    {
        capture.set(CV_CAP_PROP_POS_FRAMES, segmentStartTimes[*it]*frameRate);
        for (int i = segmentStartTimes[*it]; i < segmentStartTimes[*it+1]; i++)
        {
              for (int j = 0; j < frameRate; j++)
              {
                  capture >> frame;
                  cv::putText(frame, "Time: " + IntToString(i) + " seconds", cvPoint(30,30),
                      cv::FONT_HERSHEY_COMPLEX_SMALL, 0.8, cvScalar(200,200,250), 1, CV_AA);
                  if (frame.data)
                      cv::imshow("Sumamry Video", frame);
                  if (videoFileSave != "")
                    videoWriter.write(frame);
                  // Press  ESC on keyboard to exit
                  char c = (char)cv::waitKey(25);
                  if(c==27)
                      break;
              }
        }
    }
    capture.release();
}

void DeepSimVideoSummarizer::displayAndSaveSummaryMontage(char* imageFileSave, int image_size)
{
    int summary_x = ceil(sqrt(summarySet.size()));
    int summary_y = ceil(summarySet.size()/summary_x);
    std::vector<cv::Mat> summaryimages = std::vector<cv::Mat>();
    cv::VideoCapture capture(videoFile);
    cv::Mat frame;
    capture.set(CV_CAP_PROP_POS_FRAMES, 0);
    for (std::set<int>::iterator it = summarySet.begin(); it != summarySet.end(); it++)
    {
        capture.set(CV_CAP_PROP_POS_FRAMES, segmentStartTimes[*it]*frameRate);
        capture >> frame;
        summaryimages.push_back(frame);
    }
    capture.release();
    cv::Mat collagesummary = cv::Mat(image_size*summary_y,image_size*summary_x,CV_8UC3);
    tile(summaryimages, collagesummary, summary_x, summary_y, summaryimages.size());
    cv::imshow("Summary Collage",collagesummary);
    if (imageFileSave != "")
        cv::imwrite(imageFileSave, collagesummary);
    char c = (char)cv::waitKey(0);
    if(c==27)
        return;
}
