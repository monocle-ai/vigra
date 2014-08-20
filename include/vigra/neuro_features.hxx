/************************************************************************/
/*                                                                      */
/*                 Copyright 2011 by Ullrich Koethe                     */
/*                                                                      */
/*    This file is part of the VIGRA computer vision library.           */
/*    The VIGRA Website is                                              */
/*        http://hci.iwr.uni-heidelberg.de/vigra/                       */
/*    Please direct questions, bug reports, and contributions to        */
/*        ullrich.koethe@iwr.uni-heidelberg.de    or                    */
/*        vigra@informatik.uni-hamburg.de                               */
/*                                                                      */
/*    Permission is hereby granted, free of charge, to any person       */
/*    obtaining a copy of this software and associated documentation    */
/*    files (the "Software"), to deal in the Software without           */
/*    restriction, including without limitation the rights to use,      */
/*    copy, modify, merge, publish, distribute, sublicense, and/or      */
/*    sell copies of the Software, and to permit persons to whom the    */
/*    Software is furnished to do so, subject to the following          */
/*    conditions:                                                       */
/*                                                                      */
/*    The above copyright notice and this permission notice shall be    */
/*    included in all copies or substantial portions of the             */
/*    Software.                                                         */
/*                                                                      */
/*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND    */
/*    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES   */
/*    OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND          */
/*    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT       */
/*    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,      */
/*    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING      */
/*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR     */
/*    OTHER DEALINGS IN THE SOFTWARE.                                   */
/*                                                                      */
/************************************************************************/



#ifndef NEURO_CLUSTER_OPERATOR
#define NEURO_CLUSTER_OPERATOR



/*std*/
#include <queue>          
#include <iomanip>

/*vigra*/
#include "priority_queue.hxx"
#include "metrics.hxx" 
#include "merge_graph_adaptor.hxx"
#include "adjacency_list_graph.hxx"
#include "random_forest.hxx"
#include "random_forest/rf_visitors.hxx"

namespace vigra{      




    class NeuroDynamicFeatures{
    public:
        typedef NeuroDynamicFeatures SelfType;
        typedef AdjacencyListGraph Graph;
        typedef MergeGraphAdaptor<Graph> MergeGraph;



        typedef typename MergeGraph::MergeNodeCallBackType MergeNodeCallBackType;
        typedef typename MergeGraph::MergeEdgeCallBackType MergeEdgeCallBackType;
        typedef typename MergeGraph::EraseEdgeCallBackType EraseEdgeCallBackType;


        typedef MergeGraph::Edge MgEdge;
        typedef MergeGraph::Node MgNode;
        typedef Graph::Edge Edge;
        typedef Graph::Node Node;
        typedef MergeGraph::index_type index_type;


        NeuroDynamicFeatures(const Graph & graph, MergeGraph & mergeGraph)
        :   graph_(&graph),
            mergeGraph_(&mergeGraph){
                rf_ = NULL;
        }

        size_t initalTrainignSetSize(){
            size_t nl0=0;
            size_t nl1=0;
            size_t nl2=0;

            // count how many edges have labels
            for(size_t ei=0; ei< graph_->edgeNum(); ++ei){
                if(edgeLabelProb_(ei,0)>=0.5){
                   ++nl0;
                }
                else if(edgeLabelProb_(ei,1)>=0.5){
                   ++nl1;
                }
                else if(edgeLabelProb_(ei,2)>=0.5){
                   ++nl2;
                }
            }
            return nl0 + nl1;
        }

        void computeInitalTrainingSet(
            MultiArrayView<2, float> features,
            MultiArrayView<2, UInt32> labels
        ){
            size_t c=0;
            for(size_t ei=0; ei< graph_->edgeNum(); ++ei){

                //std::cout<<ei<<"/ "<<graph_->edgeNum()<<"\n";
                size_t l = 2;
                if(edgeLabelProb_(ei,0)>=0.5){
                   l=0;
                }
                else if(edgeLabelProb_(ei,1)>=0.5){
                   l=1;
                }

                // compute features only for instances with labels
                if(l != 2){

                    // get features
                    MultiArrayView<1,float> sampleFeat = features.bindInner(c);
                    computeFeature(graph_->edgeFromId(ei), sampleFeat);
                    // write labels
                    labels(c,0) = l;
                    ++c;
                }
            }
        }

        void assignEdgeCues(const  MultiArrayView<3, float> & edgeCues ){
            edgeCues_= edgeCues;
        }
        void assignNodeCues(const  MultiArrayView<3, float> & nodeCues ){
            nodeCues_ = nodeCues;
        }
        void assignEdgeSizes(const MultiArrayView<1, float> & edgeSizes){
            edgeSizes_ = edgeSizes;
            edgeElements_.reshape(edgeSizes_.shape());
            edgeElements_ = 1;
        }
        void assignNodeSizes(const MultiArrayView<1, float> & nodeSizes){
            nodeSizes_ = nodeSizes;
        }

        void assignLabels(const MultiArrayView<1, UInt32> & edgeLabels){
            edgeLabelProb_.reshape(
                MultiArrayView<2, UInt32>::difference_type(edgeLabels.shape(0),4)
            );
            for(size_t i=0; i<edgeLabels.shape(0); ++i){
                // edge is inactive
                if(edgeLabels(i)==0){
                    edgeLabelProb_(i,0) = 1.0;
                    edgeLabelProb_(i,1) = 0.0;
                    edgeLabelProb_(i,2) = 0.0;
                }
                else if(edgeLabels(i)==1){
                    edgeLabelProb_(i,0) = 0.0;
                    edgeLabelProb_(i,1) = 1.0;
                    edgeLabelProb_(i,2) = 0.0;
                }
                else{
                    edgeLabelProb_(i,0) = 0.0;
                    edgeLabelProb_(i,1) = 0.0;
                    edgeLabelProb_(i,2) = 1.0;
                }
            }
        }



        size_t getNewFeatureByClustering(
            const RandomForest<unsigned int> & rf,
            MultiArrayView<2, float> features,
            MultiArrayView<2, UInt32> labels
        ){
            rf_ = &rf;
            std::cout<<"getNewFeatureByClustering\n";
            ChangeablePriorityQueue< float > pq(graph_->edgeNum());
            pq_ = &pq;
            vigra::MultiArray<1, float> feat = vigra::MultiArray<1, float>(vigra::MultiArray<1, float>::difference_type(numberOfFeatures()));
            vigra::MultiArray<2, float> probs(vigra::MultiArray<2, float>::difference_type(1, 2));

            std::cout<<"fill inital priority queue\n";
            for(size_t eId=0; eId< graph_->edgeNum(); ++eId){

                computeFeature(mergeGraph_->edgeFromId(eId), feat);
                rf.predictProbabilities(feat.insertSingletonDimension(0), probs);
                const float edgeProb = probs[1];
                //std::cout<<"edgeProb "<<edgeProb<<"\n";

                pq_->push(eId, edgeProb);
            }       

            size_t newFeatCount=0;
            size_t nDisagree=0;
            while(mergeGraph_->edgeNum()>0){

                if(mergeGraph_->edgeNum()%500==0)
                    std::cout<<"number of edges: "<<mergeGraph_->edgeNum()<<"\n";
                if(pq_->topPriority()<=1.0001){
                    size_t minId = pq_->top();
                    while(mergeGraph_->hasEdgeId(minId)==false){
                        pq_->deleteItem(minId);
                        minId = pq_->top();
                    }
                    if(pq_->topPriority()>1.001){
                        std::cout<<"ONLY DISAGREEMENT NODES LEFT\n";
                        break;
                    }

                    size_t l=2;
                    // get the label of the edge
                    if(edgeLabelProb_(minId,0)>0.9){
                        //std::cout<<"AGREEMENT\n";
                        l=0;
                        if(edgeElements_(minId)>1){

                            computeFeature(mergeGraph_->edgeFromId(minId), feat);
                            features.bindInner(newFeatCount)=feat;
                            //std::cout<<"newFeatCount "<<newFeatCount<<"\n";
                            //std::cout<<"labels.shape() "<<labels.shape(0)<<"\n";
                            labels(newFeatCount++)=l;
                        }
                        mergeGraph_->contractEdge(mergeGraph_->edgeFromId(minId));
                    }
                    else if(edgeLabelProb_(minId,1)>0.9){
                        ++nDisagree;
                        l=1;
                        if(edgeElements_(minId)>1){
                            // recompute
                            computeFeature(mergeGraph_->edgeFromId(minId), feat);
                            features.bindInner(newFeatCount)=feat;
                            labels(newFeatCount++)=l;   
                        }
                        pq_->push(minId,2.0);
                    }
                    else{
                        //std::cout<<"UNKNOWN\n";
                        mergeGraph_->contractEdge(mergeGraph_->edgeFromId(minId));
                    }
                }
                else{
                    std::cout<<"ONLY DISAGREEMENT NODES LEFT\n";
                    break;
                }



            }
            std::cout<<"nDisagrees "<<nDisagree<<"\n";

            std::cout<<"delete stuff\n";

            return newFeatCount;

        }

        size_t maxNodeId()const{
            return graph_->maxNodeId();
        }

        void predict(
            const RandomForest<unsigned int> & rf,
            const float stopProb,
            MultiArrayView<1, UInt32 > & nodeLabels
        ){
            std::cout<<"predict\n";
            ChangeablePriorityQueue< float > pq(graph_->edgeNum());
            rf_ = &rf;
            pq_ = &pq;
            vigra::MultiArray<1, float> feat = vigra::MultiArray<1, float>(vigra::MultiArray<1, float>::difference_type(numberOfFeatures()));
            vigra::MultiArray<2, float> probs(vigra::MultiArray<2, float>::difference_type(1, 2));

            std::cout<<"fill inital priority queue\n";
            for(size_t eId=0; eId< graph_->edgeNum(); ++eId){

                computeFeature(mergeGraph_->edgeFromId(eId), feat);
                rf.predictProbabilities(feat.insertSingletonDimension(0), probs);
                const float edgeProb = probs[1];
                pq_->push(eId, edgeProb);
            }

            while(pq_->topPriority()<stopProb){

                if(mergeGraph_->edgeNum()%500==0){
                    std::cout<<"#edges: "<<mergeGraph_->edgeNum()<<"  ";
                    std::cout<<" prob : "<< pq_->topPriority() <<"\n";
                }
                size_t minId = pq_->top();
                bool exitIt=false;
                while(mergeGraph_->hasEdgeId(minId)==false){
                    pq_->deleteItem(minId);
                    minId = pq_->top();
                    if(pq_->topPriority()>=stopProb){
                        std::cout<<"exit it\n";
                        exitIt=true;
                        break;
                    }
                }
                if(exitIt)
                    break;
                mergeGraph_->contractEdge(mergeGraph_->edgeFromId(minId));
            }       
            std::cout<<"done with contraction\n";
            for(size_t nid=0; nid<=graph_->maxNodeId(); ++nid){
                if(graph_->nodeFromId(nid)!=lemon::INVALID){
                    nodeLabels[nid]=mergeGraph_->reprNodeId(nid);
                }
            }
        }



        void mergeNodes(const MgNode & nodeA, const MgNode & nodeB){
            const index_type idA  = mergeGraph_->id(nodeA);
            const index_type idB  = mergeGraph_->id(nodeB);
            //std::cout<<"merge nodes "<<idA<<" "<<idB<<"\n";

            // merge node sizes
            const float sizeA = nodeSizes_[idA];
            const float sizeB = nodeSizes_[idB];
            const float sizeAB = sizeA + sizeB;
            nodeSizes_[idA] = sizeAB;



            const size_t nCueTypes = nodeCues_.shape(1);
            for(size_t c=0; c<nCueTypes; ++c){

                const float meanA = nodeCues_(idA,c,0);
                const float meanB = nodeCues_(idA,c,0);
                const float varA  = nodeCues_(idA,c,1);
                const float varB  = nodeCues_(idA,c,1);
               

                // merge mean and variance
                const float  fac =  1.0/(sizeAB - 1);
                const float meanAB = (sizeA*meanA + sizeB*meanB)/sizeAB;
                const float varAB = fac * (sizeA * std::pow(meanA - meanAB, 2) + sizeB * std::pow(meanB - meanAB, 2)   +  (sizeA-1)*varA + (sizeB-1)*varB );

                nodeCues_(idA, c, 0) = meanAB;
                nodeCues_(idA, c, 1) = varAB;

                // merge the histograms
                for(size_t hb=2 ;hb<nodeCues_.shape(2); ++hb){
                    nodeCues_(idA, c, hb)  = (sizeA*nodeCues_(idA, c, hb) + sizeB*nodeCues_(idB, c, hb))/sizeAB;
                }
            }
            

        }   
        void mergeEdges(const MgEdge & edgeA, const MgEdge & edgeB){
            const index_type idA  = mergeGraph_->id(edgeA);
            const index_type idB  = mergeGraph_->id(edgeB);
            //std::cout<<"    merge edges "<<idA<<" "<<idB<<"\n";

            // merge edge sizes
            const float sizeA = edgeSizes_[idA];
            const float sizeB = edgeSizes_[idB];
            const float sizeAB = sizeA + sizeB;
            edgeSizes_[idA] = sizeAB;


            edgeElements_[idA]+=edgeElements_[idB];

            const size_t nCueTypes = edgeCues_.shape(1);
            for(size_t c=0; c<nCueTypes; ++c){

                const float meanA = edgeCues_(idA,c,0);
                const float meanB = edgeCues_(idA,c,0);
                const float varA  = edgeCues_(idA,c,1);
                const float varB  = edgeCues_(idA,c,1);
               

                // merge mean and variance
                const float  fac =  1.0/(sizeAB - 1);
                const float meanAB = (sizeA*meanA + sizeB*meanB)/sizeAB;
                const float varAB = fac * (sizeA * std::pow(meanA - meanAB, 2) + sizeB * std::pow(meanB - meanAB, 2)   +  (sizeA-1)*varA + (sizeB-1)*varB );

                edgeCues_(idA, c, 0) = meanAB;
                edgeCues_(idA, c, 1) = varAB;

                // merge the histograms
                for(size_t hb=2 ;hb<edgeCues_.shape(2); ++hb){
                    edgeCues_(idA, c, hb)  = (sizeA*edgeCues_(idA, c, hb) + sizeB*edgeCues_(idB, c, hb))/sizeAB;
                }
            }
            
            // merge labels????
            if(edgeLabelProb_.size()>0){
                edgeLabelProb_(idA,0) = (sizeA*edgeLabelProb_(idA,0) + sizeB*edgeLabelProb_(idB,0))/sizeAB;
                edgeLabelProb_(idA,1) = (sizeA*edgeLabelProb_(idA,1) + sizeB*edgeLabelProb_(idB,1))/sizeAB;
                edgeLabelProb_(idA,2) = (sizeA*edgeLabelProb_(idA,2) + sizeB*edgeLabelProb_(idB,2))/sizeAB;
            }
        }
        

        void eraseEdge(const MgEdge & edge){

            const index_type id  = mergeGraph_->id(edge);
            //std::cout<<"    erase edge "<<id<<"\n";
            pq_->deleteItem(edge.id());
            const MgNode newNode = mergeGraph_->inactiveEdgesNode(edge);

            vigra::MultiArray<1, float> feat = vigra::MultiArray<1, float>(vigra::MultiArray<1, float>::difference_type(numberOfFeatures()));
            vigra::MultiArray<2, float> probs(vigra::MultiArray<2, float>::difference_type(1, 2));


            for (MergeGraph::IncEdgeIt e(*mergeGraph_,newNode); e!=lemon::INVALID; ++e){
                const MgEdge incEdge(*e);
                computeFeature(incEdge, feat);
                rf_->predictProbabilities(feat.insertSingletonDimension(0), probs);
                //std::cout<<"new predicted prob "<<probs[1]<< "old "<<pq_->priority(incEdge.id())<<"\n";
                pq_->push(incEdge.id(),probs[1]);
            }
        }



        void registerCallbacks(){
            MergeNodeCallBackType cbMn(MergeNodeCallBackType:: template from_method<SelfType,&SelfType::mergeNodes>(this));
            MergeEdgeCallBackType cbMe(MergeEdgeCallBackType:: template from_method<SelfType,&SelfType::mergeEdges>(this));
            EraseEdgeCallBackType cbEe(EraseEdgeCallBackType:: template from_method<SelfType,&SelfType::eraseEdge>(this));

            mergeGraph_->registerMergeNodeCallBack(cbMn);
            mergeGraph_->registerMergeEdgeCallBack(cbMe);
            mergeGraph_->registerEraseEdgeCallBack(cbEe);
        }


        size_t numberOfFeatures()const{
            const size_t nEdgeCueFeatures = edgeCues_.shape(1)*edgeCues_.shape(2);
            const size_t nNodeDiffFeatures = nodeCues_.shape(1)*2;
            const size_t nDegreeFeatures = 5;
            const size_t nSizeFeatures = 24;
            return  nEdgeCueFeatures + nNodeDiffFeatures + nDegreeFeatures + nSizeFeatures;
        }

        size_t edgeNum()const{
            return graph_->edgeNum();
        }

        void computeFeature(
            const MgEdge & edge, 
            MultiArrayView<1, float> & features
        ){
            size_t featureIndex = 0;

            edgeCueFeatures(edge, features, featureIndex);
            nodeDiffFeatures(edge, features, featureIndex);
            degreeFeatures(edge, features, featureIndex);
            sizeFeatures(edge, features, featureIndex);
        }


        void nodeDiffFeatures(
            const MgEdge & edge, 
            MultiArrayView<1, float> & feature,
            size_t & featureIndex
        ){
            const int edgeId = mergeGraph_->id(edge);
            const Node u = mergeGraph_->u(edge);
            const Node v = mergeGraph_->v(edge);

            const int uId = mergeGraph_->id(u);
            const int vId = mergeGraph_->id(v);

            for (int qtype = 0;  qtype  < edgeCues_.shape(1); ++qtype){

                feature[featureIndex++] = std::abs(nodeCues_(uId, qtype, 0) - nodeCues_(vId, qtype, 0));
                feature[featureIndex++] = std::abs(nodeCues_(uId, qtype, 1) - nodeCues_(vId, qtype, 1));
            }
        }

        void edgeCueFeatures(
            const MgEdge & edge, 
            MultiArrayView<1, float> & feature,
            size_t & featureIndex
        ){
            const int edgeId = mergeGraph_->id(edge);

            for (int i = 0; i < edgeCues_.shape(1); ++i)
            for (int j = 0; j < edgeCues_.shape(2); ++j){
                feature[featureIndex++] = edgeCues_(edgeId, i, j);
            }
        }

        void degreeFeatures(
            const MgEdge & edge, 
            MultiArrayView<1, float> & feature,
            size_t & featureIndex
        ){
            const MgNode u = mergeGraph_->u(edge);
            const MgNode v = mergeGraph_->v(edge);
            const size_t dU = mergeGraph_->degree(u);
            const size_t dV = mergeGraph_->degree(v);

            feature[featureIndex++] = std::min(dU, dV);
            feature[featureIndex++] = std::max(dU, dV);
            feature[featureIndex++] = float(std::max(dU, dV))/std::min(dU, dV);
            feature[featureIndex++] = std::max(dU, dV)-std::min(dU, dV);
            feature[featureIndex++] = dU + dV;
        }

        void sizeFeatures(
            const MgEdge & edge, 
            MultiArrayView<1, float> & feature,
            size_t & featureIndex
        ){
            const MgNode u = mergeGraph_->u(edge);
            const MgNode v = mergeGraph_->v(edge);

            const float edgeSize = edgeSizes_[mergeGraph_->id(edge)];

            const float sizeU = nodeSizes_[mergeGraph_->id(u)];
            const float sizeV = nodeSizes_[mergeGraph_->id(v)];
            const float sizeUV = sizeU+sizeV;
            const float maxSize = std::max(sizeU,sizeV);
            const float minSize = std::min(sizeU,sizeV);

            const float surfSizeU = computeSurfaceSize(u);
            const float surfSizeV = computeSurfaceSize(v);
            const float surfSizeUV = surfSizeU + surfSizeV;
            const float maxSurfSize = std::max(surfSizeU,surfSizeV);
            const float minSurfSize = std::min(surfSizeU,surfSizeV);

            const float sizeSurfRatioU = std::pow(sizeU, 1.0/3.0) / std::sqrt(surfSizeU);
            const float sizeSurfRatioV = std::pow(sizeV, 1.0/3.0) / std::sqrt(surfSizeV);
            const float sizeSurfRatioUV = std::pow(sizeUV, 1.0/3.0) / std::sqrt(surfSizeUV-2.0*edgeSize);

            // node size
            feature[featureIndex++] = sizeUV;
            feature[featureIndex++] = maxSize;
            feature[featureIndex++] = minSize;
            
            // surface size
            feature[featureIndex++] = surfSizeUV;
            feature[featureIndex++] = maxSurfSize;
            feature[featureIndex++] = minSurfSize;

            // size surface ratios
            feature[featureIndex++] = sizeSurfRatioU+sizeSurfRatioV;
            feature[featureIndex++] = std::min(sizeSurfRatioU, sizeSurfRatioV);
            feature[featureIndex++] = std::max(sizeSurfRatioU, sizeSurfRatioV);
            feature[featureIndex++] = sizeSurfRatioUV;
            feature[featureIndex++] = sizeSurfRatioUV-std::min(sizeSurfRatioU, sizeSurfRatioV);
            feature[featureIndex++] = sizeSurfRatioUV-std::max(sizeSurfRatioU, sizeSurfRatioV);
            feature[featureIndex++] = sizeSurfRatioUV/std::min(sizeSurfRatioU, sizeSurfRatioV);
            feature[featureIndex++] = sizeSurfRatioUV/std::max(sizeSurfRatioU, sizeSurfRatioV);

            // edge size
            feature[featureIndex++] = edgeSize;

            // edge Size / node size ratio
            feature[featureIndex++] = edgeSize / sizeUV;
            feature[featureIndex++] = edgeSize / maxSize;
            feature[featureIndex++] = edgeSize / minSize;

            feature[featureIndex++] = edgeSize / std::pow(sizeUV , 2.0/3.0);
            feature[featureIndex++] = edgeSize / std::pow(maxSize , 2.0/3.0);
            feature[featureIndex++] = edgeSize / std::pow(minSize , 2.0/3.0);


            // edge size surfaca fraction
            feature[featureIndex++] = edgeSize / surfSizeUV;
            feature[featureIndex++] = edgeSize / maxSurfSize;
            feature[featureIndex++] = edgeSize / minSurfSize;

        }




    private:

        float computeSurfaceSize(const MgNode node)const{
            float surfSize = 0.0;
            for(MergeGraph::IncEdgeIt iter(*mergeGraph_,node); iter!=lemon::INVALID; ++iter){
                surfSize+=edgeSizes_[mergeGraph_->id(*iter)];
            }
            return surfSize;
        }


        const Graph * graph_;
        MergeGraph * mergeGraph_;


        MultiArray<1, UInt32> edgeElements_;
        MultiArray<1, float> edgeSizes_;
        MultiArray<1, float> nodeSizes_;

        MultiArray<3, float> edgeCues_;
        MultiArray<3, float> nodeCues_;



        MultiArray<2, float> edgeLabelProb_;

        const RandomForest<UInt32> * rf_;


        ChangeablePriorityQueue< float > * pq_;
    };
 
}

#endif // NEURO_CLUSTER_OPERATOR
