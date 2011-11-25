#include "clupatra_new.h"
#include "gear/GEAR.h"

#include <set>

#include <UTIL/BitField64.h>
#include <UTIL/ILDConf.h>

///---- GEAR ----
#include "marlin/Global.h"
#include "gear/TPCParameters.h"
#include "gear/PadRowLayout2D.h"
//#include "gear/BField.h"


using namespace MarlinTrk ;

namespace clupatra_new{
  
  
  /** helper class to compute the chisquared of two points in rho and z coordinate */
  struct Chi2_RPhi_Z_Hit{
    //    double operator()( const TrackerHit* h, const gear::Vector3D& v1) {
    double operator()( const ClupaHit* h, const gear::Vector3D& v1) {


      //      gear::Vector3D v0( h->getPosition()[0] ,  h->getPosition()[1] ,  h->getPosition()[2] ) ;
      const gear::Vector3D& v0 = h->pos ;

      double sigsr =  sqrt( h->lcioHit->getCovMatrix()[0] + h->lcioHit->getCovMatrix()[2] ) ;
      double sigsz =  h->lcioHit->getCovMatrix()[5] ;
      // double sigsr =  0.01 ; 
      // double sigsz =  0.1 ;
    

      double dPhi = std::abs(  v0.phi() - v1.phi() )  ;
      if( dPhi > M_PI )
	dPhi = 2.* M_PI - dPhi ;

      double dRPhi =  dPhi *  v0.rho() ; 

      double dZ = v0.z() - v1.z() ;

      return  dRPhi * dRPhi / sigsr + dZ * dZ / sigsz  ;
    }
  };

  //-------------------------------------------------------------------------------

  struct CDot{
    CDot( int i , int j , double d) : I(i) , J(j) , Dot( d ) {}
    int I ;
    int J ;
    double Dot ;
  };
  
  bool sort_CDot( const CDot& c0 ,const CDot& c1 ){
    return c0.Dot > c1.Dot ;
  }

  //-------------------------------------------------------------------------------
  

  void addHitsAndFilter( CluTrack* clu, HitListVector& hLV , double dChi2Max, double chi2Cut, unsigned maxStep, bool backward) {
    

    int static maxTPCLayerID = marlin::Global::GEAR->getTPCParameters().getPadLayout().getNRows() - 1 ; 
    
    clu->sort( LayerSortIn() ) ;
    
    int layer =  ( backward ?  clu->front()->first->layer : clu->back()->first->layer   ) ; 

    
    streamlog_out( DEBUG ) <<  " ======================  addHitsAndFilter():  - layer " << layer << "  backward: " << backward << std::endl  ;


   if( layer <= 0  || layer >=  maxTPCLayerID   ) 
	return  ;


    Chi2_RPhi_Z_Hit ch2rzh ;
    
    IMarlinTrack* trk =  clu->ext<MarTrk>() ;
    
    unsigned step = 0 ;
    
    UTIL::BitField64 encoder( ILDCellID0::encoder_string ) ; 
    encoder[ILDCellID0::subdet] = ILDDetID::TPC ;
    

    
    TrackerHit* firstHit =  0 ; 

    if( backward ) {

      // need to go back in cluster until 4th hit from the start 
      CluTrack::iterator it =  clu->begin() , end = clu->end() ;
      for( int i=0 ; it != end && i<3 ;  ++it , ++i  ) ;

      firstHit = (*it)->first->lcioHit ;

      int smoothed  = trk->smooth( firstHit ) ;
      //int smoothed  = trk->smooth() ;

     streamlog_out( DEBUG ) <<  "  -- addHitsAndFilter(): smoothed track segment : " <<  MarlinTrk::errorCode( smoothed ) << std::endl ;

    }

    
    while( step < maxStep + 1 ) {
      
      layer += ( backward ?  +1 : -1 )  ;
      
      if( layer < 0  || layer >  maxTPCLayerID   ) 
	break ;


      encoder[ ILDCellID0::layer ]  = layer ;
      
      gear::Vector3D xv ;
      
      bool hitAdded = false ;
      
      int layerID = encoder.lowWord() ;  
      int elementID = 0 ;
      
      //      int mode = ( backward ? IMarlinTrack::modeBackward : IMarlinTrack::modeForward  )  ;
      //      int mode = ( backward ? IMarlinTrack::modeForward : IMarlinTrack::modeClosest )  ;
      int mode =  IMarlinTrack::modeClosest ;



      int intersects = -1  ;
      
      if( firstHit )  {

	intersects  = trk->intersectionWithLayer( layerID, firstHit, xv, elementID , mode ) ; 

      } else {

	intersects  = trk->intersectionWithLayer( layerID, xv, elementID , mode ) ; 
      }



      streamlog_out( DEBUG ) <<  "  -- addHitsAndFilter(): looked for intersection - " 
			     <<  "  Step : " << step 
			     <<  "  at layer: "   << layer      
			     <<  "   intersects: " << MarlinTrk::errorCode( intersects )
			     <<  "  next xing point : " <<  xv  ;
      
      
      if( intersects == IMarlinTrack::success ) { // found a crossing point 
	
	//FIXME: this is copied from ClupatraProcessor 
	static const int ZBins = 160 ; 
	ZIndex zIndex( -2750. , 2750. ,ZBins  ) ; 
	int zIndCP = zIndex.index( xv[2] ) ;

 	HitList& hLL = hLV.at( layer ) ;
	
	double ch2Min = 1.e99 ;
	Hit* bestHit = 0 ;

	for( HitList::const_iterator ih = hLL.begin(), end = hLL.end() ; ih != end ; ++ih ){    

	  // if the z indices differ by more than one we can continue
	  if( nnclu::notInRange<-1,1>(  (*ih)->first->zIndex - zIndCP ) ) 
	    continue ;
	  
	  double ch2 = ch2rzh( (*ih)->first , xv )  ;
	  
	  if( ch2 < ch2Min ){

	    ch2Min = ch2 ;
 	    bestHit = (*ih) ;
 	  }
 	}//-------------------------------------------------------------------
	

  	streamlog_out( DEBUG ) <<   " ************ bestHit "  << bestHit 
			       <<   " pos : " <<   (bestHit ? bestHit->first->pos :  gear::Vector3D() ) 
			       <<   " chi2: " <<  ch2Min 
			       <<   " chi2Cut: " <<  chi2Cut <<   std::endl ;
	
 	if( bestHit != 0 ){
	  
	  const gear::Vector3D&  hPos = bestHit->first->pos  ;
	  
	  if( ch2Min  < chi2Cut ) { 
	    
	    double deltaChi = 0. ;  
	    
	    int addHit = trk->addAndFit( bestHit->first->lcioHit, deltaChi, dChi2Max ) ;
	    
	    
	    
	    streamlog_out( DEBUG ) <<   " *****       assigning left over hit : " << errorCode( addHit )  //<< hPos << " <-> " << xv
				   <<   " dist: " <<  (  hPos - xv ).r()
				   <<   " chi2: " <<  ch2Min 
				   <<   "  hit errors :  rphi=" <<  sqrt( bestHit->first->lcioHit->getCovMatrix()[0] 
									  + bestHit->first->lcioHit->getCovMatrix()[2] ) 
				   <<	 "  z= " <<  sqrt( bestHit->first->lcioHit->getCovMatrix()[5] )
				   << std::endl ;
	    
	    



	    if( addHit  == IMarlinTrack::success ) {

	      hitAdded = true ;
	      
	      hLL.remove(  bestHit ) ;
	      clu->addElement( bestHit ) ;
	      
	      
	      firstHit = 0 ; // after we added a hit, the next intersection search should use this last hit...
	      
	      
	      streamlog_out( DEBUG ) <<   " ---- track state filtered with new hit ! ------- " << std::endl ;
	    }
	  } // chi2Cut 
	} // bestHit
	

      } else {  // no intersection found 
	
	// we stop searching here
	step = maxStep + 1 ;
      }

      // reset the step or increment it
      step = (  hitAdded  ?  1 :  step + 1 ) ;

    } // while step < maxStep
  

  }

  //------------------------------------------------------------------------------------------------------------
  
  bool addHitAndFilter( int detectorID, int layer, CluTrack* clu, HitListVector& hLV , double dChi2Max, double chi2Cut) {
    
    Chi2_RPhi_Z_Hit ch2rzh ;
    
    IMarlinTrack* trk =  clu->ext<MarTrk>() ;
    
    UTIL::BitField64 encoder( ILDCellID0::encoder_string ) ; 
    
    encoder[ ILDCellID0::subdet ] = detectorID ;
    encoder[ ILDCellID0::layer  ] = layer ;
    
    int layerID = encoder.lowWord() ;  
    
    gear::Vector3D xv ;
    
    int elementID = -1 ;
    
    int intersects = trk->intersectionWithLayer( layerID, xv, elementID , IMarlinTrack::modeClosest  ) ; 
    
    //  IMarlinTrack::modeBackward , IMarlinTrack::modeForward 
    
    streamlog_out( DEBUG4 ) <<  "  ============ addHitAndFilter(): looked for intersection - " 
			    <<  "  detector : " <<  detectorID 
			    <<  "  at layer: "   << layer      
			    <<  "  intersects: " << MarlinTrk::errorCode( intersects )
			    <<  "  next xing point : " <<  xv  ;
    
    bool hitAdded = false ;
    
    if( intersects == IMarlinTrack::success ) { // found a crossing point 
      
      //FIXME: this is copied from ClupatraProcessor 
      static const int ZBins = 160 ; 
      ZIndex zIndex( -2750. , 2750. ,ZBins  ) ; 
      int zIndCP = zIndex.index( xv[2] ) ;
      
      HitList& hLL = hLV.at( layer ) ;
      
      double ch2Min = 1.e99 ;
      Hit* bestHit = 0 ;
      
      for( HitList::const_iterator ih = hLL.begin(), end = hLL.end() ; ih != end ; ++ih ){    
	
	// if the z indices differ by more than one we can continue
	if( nnclu::notInRange<-1,1>(  (*ih)->first->zIndex - zIndCP ) ) 
	  continue ;
	
	double ch2 = ch2rzh( (*ih)->first , xv )  ;
	
	if( ch2 < ch2Min ){
	  
	  ch2Min = ch2 ;
	  bestHit = (*ih) ;
	}
      }//-------------------------------------------------------------------
      
      
      streamlog_out( DEBUG3 ) <<   " ************ bestHit "  << bestHit 
			     <<   " pos : " <<   (bestHit ? bestHit->first->pos :  gear::Vector3D() ) 
			     <<   " chi2: " <<  ch2Min 
			     <<   " chi2Cut: " <<  chi2Cut <<   std::endl ;
      
      if( bestHit != 0 ){
	
	const gear::Vector3D&  hPos = bestHit->first->pos  ;
	
	if( ch2Min  < chi2Cut ) { 
	  
	  double deltaChi = 0. ;  
	  
	  int addHit = trk->addAndFit( bestHit->first->lcioHit, deltaChi, dChi2Max ) ;
	  
	  
	  
	  streamlog_out( DEBUG3 ) <<   " *****       assigning left over hit : " << errorCode( addHit )  //<< hPos << " <-> " << xv
				 <<   " dist: " <<  (  hPos - xv ).r()
				 <<   " chi2: " <<  ch2Min 
				 <<   "  hit errors :  rphi=" <<  sqrt( bestHit->first->lcioHit->getCovMatrix()[0] 
									+ bestHit->first->lcioHit->getCovMatrix()[2] ) 
				 <<	 "  z= " <<  sqrt( bestHit->first->lcioHit->getCovMatrix()[5] )
				 << std::endl ;
	  
	  
	  
	  
	  
	  if( addHit  == IMarlinTrack::success ) {
	    
	    hitAdded = true ;
	    
	    hLL.remove(  bestHit ) ;
	    clu->addElement( bestHit ) ;
	    
	    
	    streamlog_out( DEBUG4 ) <<   " ---- track state filtered with new hit ! ------- " << std::endl ;
	  }
	} // chi2Cut 
      } // bestHit
      
    } // intersection found

    return hitAdded ;
  }


  //------------------------------------------------------------------------------------------------------------

  void getHitMultiplicities( CluTrack* clu, std::vector<int>& mult ){
    
    // int static maxTPCLayerID = marlin::Global::GEAR->getTPCParameters().getPadLayout().getNRows() - 1 ; 
    // HitListVector hLV( maxTPCLayerID )  ;
    // addToHitListVector( clu->begin(), clu->end(),  hLV ) ;
    
    std::map<int, unsigned > hm ;
    
    for( CluTrack::iterator it=clu->begin(), end =clu->end() ;   it != end ; ++ it ){
      
      ++hm[ (*it)->first->layer ] ;
    }

    unsigned maxN = mult.size() - 1 ;

    //    for( unsigned i=0 ; i < hLV.size() ; ++i ){
    //      unsigned m =  hLV[i].size() ;  
      
    for( std::map<int, unsigned>::iterator it= hm.begin() , end = hm.end() ; it != end ; ++ it ){
      
      unsigned m = ( it->second < maxN ?  it->second  :  maxN   ) ;
      
      if( m == 0 ) 
	continue ;
      
      ++mult[m] ;
      
      ++mult[0] ;
    }
  }


  //------------------------------------------------------------------------------------------------------------------------- 

  void create_three_clusters( Clusterer::cluster_type& hV, Clusterer::cluster_list& cluVec ) {
    
    hV.freeElements() ;
    
    int static tpcNRow = marlin::Global::GEAR->getTPCParameters().getPadLayout().getNRows() ; 
    
    HitListVector hitsInLayer( tpcNRow )  ; 
    addToHitListVector(  hV.begin(), hV.end(), hitsInLayer ) ;
    
    CluTrack* clu[3] ;

    gear::Vector3D lastp[3] ;
    //    gear::Vector3D cluDir[3] ;

    clu[0] = new CluTrack ;
    clu[1] = new CluTrack ;
    clu[2] = new CluTrack ;
    
    cluVec.push_back( clu[0] ) ;
    cluVec.push_back( clu[1] ) ;
    cluVec.push_back( clu[2] ) ;

       
    for( int l=tpcNRow-1 ; l >= 0 ; --l){
      
      HitList& hL = hitsInLayer[ l ] ;
      
      streamlog_out(  DEBUG ) << " create_three_clusters  --- layer " << l  <<  " size: " << hL.size() << std::endl ;
      
      if( hL.size() != 3 ){
	
	continue ;
      }
      
      HitList::iterator iH = hL.begin() ;
      
      Hit* h[3] ;
      h[0] = *iH++ ;
      h[1] = *iH++   ;
      h[2] = *iH   ;
      
      gear::Vector3D p[3] ;
      p[0] = h[0]->first->pos ;
      p[1] = h[1]->first->pos ;
      p[2] = h[2]->first->pos ;
      
      
      streamlog_out(  DEBUG ) << " create_three_clusters  ---  layer " << l 
  			      << "  h0 : " << p[0] 
  			      << "  h1 : " << p[1] 
  			      << "  h2 : " << p[2] ;
      //			       << std::endl ;
      
      if( clu[0]->empty() ){ // first hit triplet
	
  	streamlog_out(  DEBUG ) << " create_three_clusters  --- initialize clusters " << std::endl ;
	
  	clu[0]->addElement( h[0] ) ;
  	clu[1]->addElement( h[1] ) ;
  	clu[2]->addElement( h[2] ) ;
	
  	// lastp[0] =  p[0] ;
  	// lastp[1] =  p[1] ;
  	// lastp[2] =  p[2] ;
	
  	lastp[0] = ( 1. / p[0].r() ) * p[0] ;
  	lastp[1] = ( 1. / p[1].r() ) * p[1] ;
  	lastp[2] = ( 1. / p[2].r() ) * p[2] ;
	
  	continue ;  
      }
      

      // unit vector in direction of current hits
      gear::Vector3D pu[3] ;
      pu[0] = ( 1. / p[0].r() ) * p[0] ;
      pu[1] = ( 1. / p[1].r() ) * p[1] ;
      pu[2] = ( 1. / p[2].r() ) * p[2] ;
      

      std::list< CDot > cDot ; 

      // create list of dot products between last hit in cluster and current hit 
      // cos( angle between  hits as seen from IP ) 
      for( int i = 0 ; i < 3 ; ++i ){	         
  	for( int j = 0 ; j < 3 ; ++j ){
	  
  	  // // unit vector in direction of last hit and 
  	  // gear::Vector3D pu = - ( p[0] - lastp[0] ) ;
  	  // pu[1] = - ( p[1] - lastp[1] ) ;
  	  // pu[2] = - ( p[2] - lastp[2] ) ;
  	  // pu[0] =  (  1. / pu[0].r() ) * pu[0]  ;
  	  // pu[1] =  (  1. / pu[1].r() ) * pu[1]  ;
  	  // pu[2] =  (  1. / pu[2].r() ) * pu[2]  ;
  	  // cDot.push_back( CDot( i , j , cluDir[i].dot( pu[ j ] ) )) ;
	  
  	  cDot.push_back( CDot( i , j , lastp[i].dot( pu[ j ] ) )) ;
	  
  	}   
      }   
      
      // sort dot products in descending order 
      cDot.sort( sort_CDot ) ;

      // assign hits to clusters with largest dot product ( smallest angle )

      std::set<int> cluIdx ;
      std::set<int> hitIdx ;

      for( std::list<CDot>::iterator it = cDot.begin() ; it != cDot.end() ; ++ it ) {
	
  	streamlog_out(  DEBUG ) << " I : " << it->I  
  				<< " J : " << it->J 
  				<< " d : " << it->Dot 
  				<< std::endl ;
	
  	int i =  it->I ;
  	int j =  it->J ;

  	// ignore clusters or hits that hvae already been assigned
  	if( cluIdx.find( i ) == cluIdx.end()  && hitIdx.find( j ) == hitIdx.end() ){

  	  cluIdx.insert( i ) ;
  	  hitIdx.insert( j ) ;

  	  clu[ i ]->addElement( h[ j ] ) ;

  	  // cluDir[i] = - ( p[j] - lastp[i] ) ;
  	  // cluDir[i] =  (  1. / cluDir[i].r() ) * cluDir[i]  ;

  	  lastp[i] =  p[j] ;

  	  streamlog_out(  DEBUG ) << " **** adding to cluster : " << it->I  
  				  << " hit  : " << it->J 
  				  << " d : " << it->Dot 
  				  << std::endl ;
  	}
	
      }

      // lastp[0] =  pu[0] ;
      // lastp[1] =  pu[1] ;
      // lastp[2] =  pu[2] ;
      // lastp[0] =  p[0] ;
      // lastp[1] =  p[1] ;
      // lastp[2] =  p[2] ;
      
      
    }


    streamlog_out(  DEBUG ) << " create_three_clusters  --- clu[0] " << clu[0]->size() 
  			    <<  " clu[1] " << clu[1]->size() 
  			    <<  " clu[2] " << clu[2]->size() 
  			    << std::endl ;




    return ;
  }
  //-----------------------------------------------------------------

  void create_two_clusters( Clusterer::cluster_type& clu, Clusterer::cluster_list& cluVec ) {
    

    clu.freeElements() ;
    
    streamlog_out(  DEBUG ) << " create_two_clusters  --- called ! - size :  " << clu.size()  << std::endl ;

    int static tpcNRow = marlin::Global::GEAR->getTPCParameters().getPadLayout().getNRows() ; 
    
    HitListVector hitsInLayer( tpcNRow )  ; 

    addToHitListVector(  clu.begin(), clu.end(), hitsInLayer ) ;
    

    CluTrack* clu0 = new CluTrack ;
    CluTrack* clu1 = new CluTrack ;

    cluVec.push_back( clu0 ) ;
    cluVec.push_back( clu1 ) ;


    gear::Vector3D lastDiffVec(0.,0.,0.) ;
    
    for( int l=tpcNRow-1 ; l >= 0 ; --l){

      HitList& hL = hitsInLayer[ l ] ;
	
      streamlog_out(  DEBUG ) << " create_two_clusters  --- layer " << l  <<  " size: " << hL.size() << std::endl ;

      if( hL.size() != 2 ){
	
  	// if there is not exactly two hits in the layer, we simply copy the unassigned hits to the leftover hit vector
  	// std::copy( hL.begin(),  hL.end() , std::back_inserter( hV ) ) ;
  	continue ;
      }

      HitList::iterator iH = hL.begin() ;

      Hit* h0 = *iH++ ;
      Hit* h1 = *iH   ;
      
      gear::Vector3D& p0 = h0->first->pos ;
      gear::Vector3D& p1 = h1->first->pos ;


      streamlog_out(  DEBUG ) << " create_two_clusters  ---  layer " << l 
  			      << "  h0 : " << p0 
  			      << "  h1 : " << p1 ;
      //			       << std::endl ;
      
      if( clu0->empty() ){ // first hit pair
	
  	streamlog_out(  DEBUG ) << " create_two_clusters  --- initialize clusters " << std::endl ;
	
  	clu0->addElement( h0 ) ;
  	clu1->addElement( h1 ) ;
	
  	lastDiffVec = p1 - p0 ;
	
  	continue ;  
      }

      gear::Vector3D d = p1 - p0 ;
      
      float s0 =  ( lastDiffVec + d ).r() ;
      float s1 =  ( lastDiffVec - d ).r() ;
      
      if( s0 > s1 ){  // same orientation, i.e. h0 in this layer belongs to h0 in first layer
	
  	streamlog_out(  DEBUG ) << " create_two_clusters  ---   same orientation " << std::endl ;
  	clu0->addElement( h0 ) ;
  	clu1->addElement( h1 ) ;
	
      } else{                // oposite orientation, i.e. h1 in this layer belongs to h0 in first layer

  	streamlog_out(  DEBUG3 ) << " create_two_clusters  ---  oposite orientation " << std::endl ;
  	clu0->addElement( h1 ) ;
  	clu1->addElement( h0 ) ;
      }

    }

    // clu.freeHits() ;

    streamlog_out(  DEBUG1 ) << " create_two_clusters  --- clu0 " << clu0->size() 
  			     <<  " clu1 " << clu1->size() << std::endl ;

    // return std::make_pair( clu0 , clu1 ) ;

    return ;
  }

 //------------------------------------------------------------------------------------------------------------------------- 


  // /** Find the nearest hits in the previous and next layers - (at most maxStep layers appart - NOT YET).
  //  */
  // void  findNearestHits( CluTrack& clu,  int maxLayerID, int maxStep){
    
  //   HitListVector hitsInLayer( maxLayerID )  ; 
  //   addToHitListVector(  clu.begin(), clu.end(), hitsInLayer ) ;
    
  //   for(CluTrack::iterator it= clu.begin() ; it != clu.end() ; ++it ){
      
  //     Hit* hit0 = *it ; 
  //     gear::Vector3D& pos0 =  hit0->first->pos ;
      
  //     int layer = hit0->first->layerID ;
      
  //     int l=0 ;
  //     if( (l=layer+1) <  maxLayerID ) {
	
  // 	HitList& hL = hitsInLayer[ l ] ;
	
  // 	double minDist2 = 1.e99 ;
  // 	Hit*   bestHit = 0 ;
	
  // 	for( HitList::iterator iH = hL.begin() ; iH != hL.end() ; ++iH ) {
	  
  // 	  Hit* hit1 = *iH ; 
  // 	  gear::Vector3D& pos1 = hit1->first->pos ;
	  
  // 	  double dist2 = ( pos0 - pos1 ).r2() ;
	  
  // 	  if( dist2 < minDist2 ){
  // 	    minDist2 = dist2 ;
  // 	    bestHit = hit1 ;
  // 	  }
  // 	}
  // 	if( bestHit ){
	  
  // 	  hit0->first->nNNHit =  bestHit->first ;
  // 	  hit0->first->nDist  =  minDist2 ;
  // 	}
  //     }
      
  //     if( (l=layer-1) > 0  ) {
	
  // 	HitList& hL = hitsInLayer[ l ] ;
	
  // 	double minDist2 = 1.e99 ;
  // 	Hit* bestHit = 0 ;
	
  // 	for( HitList::iterator iH = hL.begin() ; iH != hL.end() ; ++iH ) {
	  
  // 	  Hit* hit1 = *iH ; 
  // 	  gear::Vector3D&  pos1 =  hit1->first->pos ;
	  
  // 	  double dist2 = ( pos0 - pos1 ).r2() ;
	  
  // 	  if( dist2 < minDist2 ){
  // 	    minDist2 = dist2 ;
  // 	    bestHit = hit1 ;
  // 	  }
  // 	}
  // 	if( bestHit ){
	  
  // 	  hit0->first->pNNHit =  bestHit->first ;
  // 	  hit0->first->pDist  =  minDist2 ;
  // 	}
  //     }
      
  //   }
  // }


  // //------------------------------------------------------------------------------------------------------------------------- 
  // //std::pair<CluTrack*, CluTrack* > create_two_clusters( CluTrack& clu,  HitVec& hV,  int maxLayerID){
  // void create_two_clusters( const HitVec& hV, CluTrackVec& cluVec,  int maxLayerID) {

 
  
}//namespace
