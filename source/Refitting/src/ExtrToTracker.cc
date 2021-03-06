#include "ExtrToTracker.h"

#include <IMPL/TrackerHitPlaneImpl.h>


// #include "TFile.h"
// #include "TTree.h"
// #include "LinkDef.h"
#include "MarlinTrk/MarlinTrkUtils.h"

#include "UTIL/Operators.h"

#include "DD4hep/LCDD.h"
#include "DD4hep/DD4hepUnits.h"
#include "DDRec/Surface.h"
#include "DDRec/SurfaceManager.h"
#include "DDRec/DetectorData.h"
#include "DDRec/DDGear.h"
#include "DD4hep/DetType.h"
#include "DD4hep/DetectorSelector.h"

#include <algorithm>

//CxxUtils/
#include "fpcompare.h"




using namespace lcio ;
using namespace marlin ;
using namespace MarlinTrk ;



//------------------------------------------------------------------------------------------

struct PtSort {  // sort tracks wtr to pt - largest first
  inline bool operator()( const lcio::LCObject* l, const lcio::LCObject* r) {      
    return CxxUtils::fpcompare::less( std::abs( ( (const lcio::Track*) l )->getOmega() ), std::abs( ( (const lcio::Track*) r )->getOmega() ) );
    //return ( std::abs( ( (const lcio::Track*) l )->getOmega() ) < std::abs( ( (const lcio::Track*) r )->getOmega() )  );  // pt ~ 1./omega  
  }
};
//------------------------------------------------------------------------------------------

struct InversePtSort {  // sort tracks wtr to pt - smallest first
  inline bool operator()( const lcio::LCObject* l, const lcio::LCObject* r) {      
    return CxxUtils::fpcompare::greater( std::abs( ( (const lcio::Track*) l )->getOmega() ), std::abs( ( (const lcio::Track*) r )->getOmega() ) );
  }
};

//------------------------------------------------------------------------------------------


//------------------------------------------------------------------------------------------

struct ZSort {  // sort track segments wtr to Z - smallest first
  inline bool operator()( const lcio::LCObject* l, const lcio::LCObject* r) {      
    return CxxUtils::fpcompare::less( std::abs( ( (const lcio::Track*) l )->getZ0() ), std::abs( ( (const lcio::Track*) r )->getZ0() ) );
    //return ( std::abs( ( (const lcio::Track*) l )->getZ0() ) < std::abs( ( (const lcio::Track*) r )->getZ0() )  );  // pt ~ 1./omega  
  }
};

//------------------------------------------------------------------------------------------


ExtrToTracker aExtrToTracker ;


ExtrToTracker::ExtrToTracker() : Processor("ExtrToTracker") {
  
  // modify processor description
  _description = "ExtrToTracker refits an input VXD track collection, and used IMarlinTrk tools to propagate it to the main tracker" ;
  
  
  // register steering parameters: name, description, class-variable, default value
  
  registerInputCollection( LCIO::TRACK,
			   "InputTrackCollectionName" , 
			   "Name of the input track collection"  ,
			   _input_track_col_name ,
			   std::string("TruthTracks") ) ;


  /////////////////////////

  StringVec vecDigiHitsDefault;
  vecDigiHitsDefault.push_back( "ITrackerHits" );
  vecDigiHitsDefault.push_back( "OTrackerHits" );
  vecDigiHitsDefault.push_back( "ITrackerEndcapHits" );
  vecDigiHitsDefault.push_back( "OTrackerEndcapHits" );

  registerInputCollections(LCIO::TRACKERHITPLANE,
                           "vecDigiHits",
                           "vector of name of the digi hits collection - nned to be syncro with vecSubdetName!",
                           _vecDigiHits,
                           vecDigiHitsDefault );


  StringVec vecSubdetNameDefault;
  vecSubdetNameDefault.push_back("InnerTrackerBarrel") ;
  vecSubdetNameDefault.push_back("OuterTrackerBarrel") ;
  vecSubdetNameDefault.push_back("InnerTrackerEndcap") ;
  vecSubdetNameDefault.push_back("OuterTrackerEndcap") ;

  registerProcessorParameter( "vecSubdetName" , 
                              "vector of names of all subdetector to exrapolate to" ,
			      _vecSubdetName ,
                              vecSubdetNameDefault );

  /////////////////////////


  registerOutputCollection( LCIO::TRACKERHITPLANE,
			    "OutputNotUsedHitCollectionName" , 
			    "Name of the output collection with the not used hits"  ,
			    _output_not_used_col_name ,
			    std::string("NotUsedHits") ) ;


  registerOutputCollection( LCIO::TRACK,
			    "OutputTrackCollectionName" , 
			    "Name of the output track collection"  ,
			    _output_track_col_name ,
			    std::string("ExtrTracks") ) ;


  registerProcessorParameter("MultipleScatteringOn",
                             "Use MultipleScattering in Fit",
                             _MSOn,
                             bool(true));
  
  registerProcessorParameter("EnergyLossOn",
                             "Use Energy Loss in Fit",
                             _ElossOn,
                             bool(true));
  
  registerProcessorParameter("SmoothOn",
                             "Smooth All Mesurement Sites in Fit",
                             _SmoothOn,
                             bool(false));
  
  registerProcessorParameter("Max_Chi2_Incr",
                             "maximum allowable chi2 increment when moving from one site to another",
                             _Max_Chi2_Incr,
                             double(1000));

  registerProcessorParameter("SearchSigma",
                             "times d0(Z0) acceptable from track extrapolation point",
                             _searchSigma,
                             double(3));

  registerProcessorParameter("PerformFinalRefit",
                             "perform a final refit of the extrapolated track",
                             _performFinalRefit,
                             bool(false));  

}


void ExtrToTracker::init() { 
  

  streamlog_out(DEBUG) << "   init called  " 
		       << std::endl ;
  
  // usually a good idea to
  printParameters() ;
  


  getGeoInfo();


  _trksystem =  MarlinTrk::Factory::createMarlinTrkSystem( "DDKalTest" , marlin::Global::GEAR , "" ) ;




  //   /////////////////////////////
	    

  
  if( _trksystem == 0 ){
    
    throw EVENT::Exception( std::string("  Cannot initialize MarlinTrkSystem of Type: ") + std::string("DDKalTest" )  ) ;
    
  }
  
  _trksystem->setOption( IMarlinTrkSystem::CFG::useQMS,        _MSOn ) ;
  _trksystem->setOption( IMarlinTrkSystem::CFG::usedEdx,       _ElossOn) ;
  _trksystem->setOption( IMarlinTrkSystem::CFG::useSmoothing,  _SmoothOn) ;
  _trksystem->init() ;  
  
  
  _n_run = 0 ;
  _n_evt = 0 ;
  SITHitsFitted = 0 ;
  SITHitsNonFitted = 0 ;
  TotalSITHits = 0 ;



  //_maxChi2PerHit = 100;
  _maxChi2PerHit = _Max_Chi2_Incr;
    
}


void ExtrToTracker::processRunHeader( LCRunHeader* run) { 
  
  ++_n_run ;
} 

void ExtrToTracker::processEvent( LCEvent * evt ) { 





  //-- note: this will not be printed if compiled w/o MARLINDEBUG=1 !
  
  streamlog_out(DEBUG1) << "   processing event: " << _n_evt 
			<< std::endl ;
  


  // get input collection and relations 
  LCCollection* input_track_col = this->GetCollection( evt, _input_track_col_name ) ;

  if( input_track_col != 0 ){
    


    ////////////////////////

    fillVecSubdet(evt);
    fillMapElHits(_vecDigiHitsCol, _vecMapsElHits);

    ////////////////////////


    // establish the track collection that will be created 
    LCCollectionVec* trackVec = new LCCollectionVec( LCIO::TRACK )  ;    

    
    // if we want to point back to the hits we need to set the flag
    LCFlagImpl trkFlag(0) ;
    trkFlag.setBit( LCIO::TRBIT_HITS ) ;
    trackVec->setFlag( trkFlag.getFlag()  ) ;
    
    int nTracks = input_track_col->getNumberOfElements()  ;

    streamlog_out(DEBUG4) << " ######### NO OF TRACKS $$$$$$$$$$ " << nTracks << std::endl;

    LCCollectionVec* inputTrackVec = new LCCollectionVec( LCIO::TRACK )  ; 

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////


    for( int n= 0 ; n < nTracks ; ++n ) {
      Track* testTrack = dynamic_cast<Track*>( input_track_col->getElementAt( n ) ) ;
      inputTrackVec->addElement( testTrack );
    }

    std::sort( inputTrackVec->begin() , inputTrackVec->end() ,  PtSort()  ) ;
    //std::sort( inputTrackVec->begin() , inputTrackVec->end() ,  InversePtSort()  ) ;


    // loop over the input tracks and refit using KalTest    
    for(int i=0; i< nTracks ; ++i) {


      
     

      int SITHitsPerTrk = 0 ;
      
      Track* track = dynamic_cast<Track*>( inputTrackVec->getElementAt( i ) ) ;
      
      MarlinTrk::IMarlinTrack* marlin_trk = _trksystem->createTrack();
     
      EVENT::TrackerHitVec trkHits = track->getTrackerHits() ;
      
	
      // sort the hits in R, so here we are assuming that the track came from the IP 
 
      sort(trkHits.begin(), trkHits.end(), ExtrToTracker::compare_r() );
	
      EVENT::TrackerHitVec::iterator it = trkHits.begin();
	
      for( it = trkHits.begin() ; it != trkHits.end() ; ++it ){
	marlin_trk->addHit(*it);
      }
	
      int init_status = FitInit2(track, marlin_trk) ;          
	
      if (init_status==0) {
	  


	streamlog_out(DEBUG4) << "track initialised " << std::endl ;
	  
	int fit_status = marlin_trk->fit(); 
	  
	if ( fit_status == 0 ){
	    
	  int testFlag=0;




	    
	  double chi2 = 0 ;
	  int ndf = 0 ;
	  TrackStateImpl trkState;	
	    
	    
	  UTIL::BitField64 encoder(  lcio::ILDCellID0::encoder_string ) ; //do not change it, code will not work with a different encoder
	    
	  encoder.reset() ;  // reset to 0
	    
	  int layerID = encoder.lowWord() ;  
	  int elementID = 0 ;    
	    
	    
	  //________________________________________________________________________________________________________
	  //
	  // starting loop on subdetectors and loop on each subdetector layer
	  //________________________________________________________________________________________________________
	      
	  //printParameters();






	  for (size_t idet=0; idet<_vecSubdetName.size(); idet++){
	    streamlog_out(DEBUG4) << "LOOP - idet = " << idet << " begins "<< std::endl;
        
	    if( _vecDigiHitsCol.at(idet) != 0 ){ 


	      for (int iL=0;iL<_vecSubdetNLayers.at(idet);iL++){
		streamlog_out(DEBUG4) << "LOOP" << iL << " begins "<< std::endl;
	 	
		encoder[lcio::ILDCellID0::subdet] = _vecSubdetID.at(idet);
		encoder[lcio::ILDCellID0::layer]  = iL;   
		layerID = encoder.lowWord();  
		streamlog_out(DEBUG4) << "layerID = " << layerID << std::endl;
		
		///////////////////////////////////////////////////////////

   

		if ( marlin_trk->propagateToLayer( layerID, trkState, chi2, ndf, elementID, IMarlinTrack::modeClosest) == MarlinTrk::IMarlinTrack::success) {
		    

		  streamlog_out(DEBUG4) << "-- layerID " << layerID << std::endl;


		  const FloatVec& covLCIO = trkState.getCovMatrix();
		  const float* pivot = trkState.getReferencePoint();
		  double r = sqrt( pivot[0]*pivot[0]+pivot[1]*pivot[1] ) ;
		  
		  streamlog_out( DEBUG4 ) << " kaltest track parameters: "
					  << " chi2/ndf " << chi2 / ndf  
					  << " chi2 " <<  chi2 << std::endl 
		    
					  << "\t D0 "          <<  trkState.getD0() <<  "[+/-" << sqrt( covLCIO[0] ) << "] " 
					  << "\t Phi :"        <<  trkState.getPhi()<<  "[+/-" << sqrt( covLCIO[2] ) << "] " 
					  << "\t Omega "       <<  trkState.getOmega() <<  "[+/-" << sqrt( covLCIO[5] ) << "] " 
					  << "\t Z0 "          <<  trkState.getZ0() <<  "[+/-" << sqrt( covLCIO[9] ) << "] " 
					  << "\t tan(Lambda) " <<  trkState.getTanLambda() <<  "[+/-" << sqrt( covLCIO[14]) << "] " 
		      
					  << "\t pivot : [" << pivot[0] << ", " << pivot[1] << ", "  << pivot[2] 
					  << " - r: " << r << "]" 
					  << std::endl ;
		  
		  
		  streamlog_out(DEBUG4) << " layer " << iL << " max search distances Z : Rphi " << _searchSigma*sqrt( covLCIO[9] ) << " : " << _searchSigma*sqrt( covLCIO[0] ) << std::endl ;


		  //_______________________________________________________________________________________
		  //
		  
		  streamlog_out(DEBUG2) << " element ID " << elementID << std::endl;
		  
		  if ( elementID != 0 ){
		    
		    testFlag = 1;
		    
		    float dU_spres = 0.007;
		    float dV_spres = 0.05;

		    bool isSuccessfulFit = false; 




		    std::string cellIDEcoding = _vecDigiHitsCol.at(idet)->getParameters().getStringVal("CellIDEncoding") ;  
		    std::vector<int > vecIDs;
		    vecIDs.push_back(elementID);
		    getNeighbours(elementID, vecIDs, cellIDEcoding, _vecMapLayerNModules.at(idet)); // TO BE IMPROVED AND STUDIED


		    TrackerHitPlane* BestHit;
		    //int nhits=0;
		    //BestHit = getSiHit(_vecDigiHitsCol.at(idet), elementID, marlin_trk, nhits);
		    //BestHit = getSiHit( _vecMapsElHits.at(idet)[elementID], marlin_trk);
		    BestHit = getSiHit(vecIDs, _vecMapsElHits.at(idet), marlin_trk);

		    if (BestHit != 0){
		      			  
		      streamlog_out(DEBUG4) << " --- Best hit found: call add and fit _Max_Chi2_Incr "<< _Max_Chi2_Incr<< std::endl ; 
						  
		      double chi2_increment = 0.;

			
		      //smearing on the hit is really needed? has not been done in digi? - turned off for the moment
		      bool doSinglePointResolutionSmearing = false; //make it a general parameter

		      if (doSinglePointResolutionSmearing){
			TrackerHitPlaneImpl *TestHitPlane = new TrackerHitPlaneImpl ;   
			TestHitPlane->setCellID0(BestHit->getCellID0()) ;
			TestHitPlane->setPosition(BestHit->getPosition());
			TestHitPlane->setdU(dU_spres);
			TestHitPlane->setdV(dV_spres);
			isSuccessfulFit = marlin_trk->addAndFit( TestHitPlane, chi2_increment, _Max_Chi2_Incr ) == IMarlinTrack::success ;
			delete TestHitPlane ;
		      } else {
			isSuccessfulFit = marlin_trk->addAndFit( BestHit, chi2_increment, _Max_Chi2_Incr ) == IMarlinTrack::success ;
			streamlog_out(DEBUG4) << " --- chi2_increment "<< chi2_increment << std::endl ; 
			streamlog_out(DEBUG4) << " --- isSuccessfulFit "<< isSuccessfulFit << std::endl ; 
		      }

		  
		
		      TotalSITHits++;
			
		      if ( isSuccessfulFit ){
			  
			streamlog_out(DEBUG4) << " successful fit " << std::endl ; 
			streamlog_out(DEBUG4) << " increment in the chi2 = " << chi2_increment << "  , max chi2 to accept the hit " << _Max_Chi2_Incr  << std::endl;
			  
			trkHits.push_back(BestHit) ;
			  
			  
			streamlog_out(DEBUG4) << " +++ hit added " << BestHit << std::endl ;
	      

			SITHitsPerTrk++;
			SITHitsFitted++;
			  
			  
		      } //end successful fit
		      else{
			  
			SITHitsNonFitted++;
			streamlog_out(DEBUG4) << " +++ HIT NOT ADDED "<< std::endl;
			  
		      }


		    }   // besthit found
		    		  

		  }//elementID !=0 
		  
		} // successful propagation to layer
		  	      
	      } // loop to all subdetector layers

	    }//end colDigi not empty 

	  }//end loop on subdetectors
	  streamlog_out(DEBUG4) << " no of hits in the track (after adding SIT hits) " << trkHits.size() << " SIT hits added " << SITHitsPerTrk  << " event " <<  _n_evt<< std::endl;
	    
	    

	  //==============================================================================================================

	  IMPL::TrackImpl* lcio_trk = new IMPL::TrackImpl();

	  IMarlinTrack* marlinTrk = 0 ;

	  if( ! _performFinalRefit ) {

	    //fg: ------ here we just create a final LCIO track from the extrapolation :
	    
	    marlinTrk = marlin_trk ;
	    
	    bool fit_direction = IMarlinTrack::forward ;
	    int return_code =  finaliseLCIOTrack( marlin_trk, lcio_trk, trkHits,  fit_direction ) ;
	    
	    streamlog_out( DEBUG ) << " *** created finalized LCIO track - return code " << return_code  << std::endl 
				   << *lcio_trk << std::endl ;
	    

	  } else { //fg: ------- perform a final refit - does not work right now ...

	    // refitted track collection creation
	    if (  testFlag==1 ){
	      
	      
	      TrackStateImpl* trkState = new TrackStateImpl() ;
	      double chi2_fin = 0. ;
	      int ndf_fin = 0 ;
	      
	      marlin_trk->getTrackState(*trkState, chi2_fin, ndf_fin);
	      //const FloatVec& covMatrix = trkState->getCovMatrix();


	      //////////////////////////////////////////////////////////////////////////////////
	      

	      sort(trkHits.begin(), trkHits.end(), ExtrToTracker::compare_r() );



	      bool fit_backwards = IMarlinTrack::backward;
	      //bool fit_forwards = IMarlinTrack::forward;
	      MarlinTrk::IMarlinTrack* marlinTrk = _trksystem->createTrack();		


	      std::vector<EVENT::TrackerHit* > vec_hits;
	      vec_hits.clear();
	      for(unsigned int ih=0; ih<trkHits.size(); ih++){
		vec_hits.push_back(trkHits.at(ih));
	      }//end loop on hits
	      streamlog_out(DEBUG) << " --- vec_hits.size() = " <<   vec_hits.size()  <<std::endl;	


	      int ndf_test_0;
	      int return_error_0 = marlinTrk->getNDF(ndf_test_0);
	      streamlog_out(DEBUG3) << "++++ 0 - getNDF returns " << return_error_0 << std::endl;
	      streamlog_out(DEBUG3) << "++++ 0 - getNDF returns ndf = " << ndf_test_0 << std::endl;


	      //Kalman filter smoothing - fit track from out to in
	      int error_fit =  createFit(vec_hits, marlinTrk, trkState, _bField, fit_backwards, _maxChi2PerHit);
	      streamlog_out(DEBUG) << "---- createFit - error_fit = " << error_fit << std::endl;

	      bool fit_direction  = fit_backwards ;

	      if (error_fit == 0) {
		int error = finaliseLCIOTrack(marlinTrk, lcio_trk, vec_hits, fit_direction );
		streamlog_out(DEBUG) << "---- finalisedLCIOTrack - error = " << error << std::endl;
		
		int ndf_test;
		int return_error = marlinTrk->getNDF(ndf_test);
		streamlog_out(DEBUG3) << "++++ getNDF returns " << return_error << std::endl;
		streamlog_out(DEBUG3) << "++++ getNDF returns ndf = " << ndf_test << std::endl;


		if (error!=0){
            
		  streamlog_out(DEBUG3) << "Error from finaliseLCIOTrack non zero! deleting tracks. error=" << error <<" noHits: "<<trkHits.size()<<" marlinTrk: "<<marlinTrk<<" lcio_trk: "<<lcio_trk<< std::endl;
            
		  delete lcio_trk;
		  continue ; 
          
		}
	      } else {
		streamlog_out(DEBUG3) << "Error from createFit non zero! deleting tracks. error_fit=" << error_fit << std::endl;
              
		delete lcio_trk;
		continue ; 
        
	      }

	      delete trkState;

	    } // end of the creation of the refitted track collection
	  } // !_perfomFinalRefit 

	      
	  // fit finished - get hits in the fit
	  
	  std::vector<std::pair<EVENT::TrackerHit*, double> > hits_in_fit;
	  std::vector<std::pair<EVENT::TrackerHit* , double> > outliers;
	  
	  // remember the hits are ordered in the order in which they were fitted
	  
	  marlinTrk->getHitsInFit(hits_in_fit);
	  
	  if( hits_in_fit.size() < 3 ) {
	    streamlog_out(DEBUG3) << "RefitProcessor: Less than 3 hits in fit: Track Discarded. Number of hits =  " << trkHits.size() << std::endl;
	    delete marlinTrk ;
	    delete lcio_trk;
	    continue ; 
	  }
	    
	    
	  std::vector<TrackerHit*> all_hits;
	  all_hits.reserve(300);
	    
	    
	  for ( unsigned ihit = 0; ihit < hits_in_fit.size(); ++ihit) {
	    all_hits.push_back(hits_in_fit[ihit].first);
	  }
	    
	  UTIL::BitField64 cellID_encoder(  lcio::ILDCellID0::encoder_string ) ; //do not change it, code will not work with a different encoder
	    
	  MarlinTrk::addHitNumbersToTrack(lcio_trk, all_hits, true, cellID_encoder);
	    
	  marlinTrk->getOutliers(outliers);
	    
	  for ( unsigned ihit = 0; ihit < outliers.size(); ++ihit) {
	    all_hits.push_back(outliers[ihit].first);
	  }
	    
	  MarlinTrk::addHitNumbersToTrack(lcio_trk, all_hits, false, cellID_encoder);
	    
	  int nhits_in_vxd = lcio_trk->subdetectorHitNumbers()[ 2 * lcio::ILDDetID::VXD - 2 ];
	  int nhits_in_ftd = lcio_trk->subdetectorHitNumbers()[ 2 * lcio::ILDDetID::FTD - 2 ];
	  int nhits_in_sit = lcio_trk->subdetectorHitNumbers()[ 2 * lcio::ILDDetID::SIT - 2 ];
	  int nhits_in_tpc = lcio_trk->subdetectorHitNumbers()[ 2 * lcio::ILDDetID::TPC - 2 ];
	  int nhits_in_set = lcio_trk->subdetectorHitNumbers()[ 2 * lcio::ILDDetID::SET - 2 ];
	    
	    
	  streamlog_out( DEBUG4 ) << " Hit numbers for Track "<< lcio_trk->id() << ": "
				  << " vxd hits = " << nhits_in_vxd
				  << " ftd hits = " << nhits_in_ftd
				  << " sit hits = " << nhits_in_sit
				  << " tpc hits = " << nhits_in_tpc
				  << " set hits = " << nhits_in_set
				  << std::endl;
	    
	    
	  if (nhits_in_vxd > 0) lcio_trk->setTypeBit( lcio::ILDDetID::VXD ) ;
	  if (nhits_in_ftd > 0) lcio_trk->setTypeBit( lcio::ILDDetID::FTD ) ;
	  if (nhits_in_sit > 0) lcio_trk->setTypeBit( lcio::ILDDetID::SIT ) ;
	  if (nhits_in_tpc > 0) lcio_trk->setTypeBit( lcio::ILDDetID::TPC ) ;
	  if (nhits_in_set > 0) lcio_trk->setTypeBit( lcio::ILDDetID::SET ) ;
	    
	  // trackCandidates.push_back(lcio_trk) ;  // trackCandidates vector stores all the candidate tracks of the event
	    
	  trackVec->addElement( lcio_trk );
	    

	  if( _performFinalRefit ) delete marlinTrk ;

	}  // good fit status
      } // good initialisation status



      delete marlin_trk;
      
    }    // for loop to the tracks 
    
    //-------------------------------------------------------------------------------------------------------		
   

    evt->addCollection( trackVec , _output_track_col_name ) ;

    //delete trackVec;
    //delete inputTrackVec;



    /////////////////////////////////////////////////////////////////
    // Save not used hits in a collection for possible further use //
    /////////////////////////////////////////////////////////////////

    LCCollectionVec* notUsedHitsVec = new LCCollectionVec( LCIO::TRACKERHITPLANE );    
    CellIDEncoder<TrackerHitPlaneImpl> cellid_encoder( lcio::ILDCellID0::encoder_string, notUsedHitsVec ) ;  //do not change it, code will not work with a different encoder
    notUsedHitsVec->setSubset(true);

    for(size_t iDet=0; iDet<_vecMapsElHits.size(); iDet++){
      for(std::map<int , std::vector<TrackerHitPlane* > >::iterator iterator = _vecMapsElHits.at(iDet).begin(); iterator != _vecMapsElHits.at(iDet).end(); iterator++) {
	std::vector<TrackerHitPlane* > vecHits = iterator->second;
	for(size_t iHitOnEl=0; iHitOnEl<vecHits.size(); iHitOnEl++){
	  std::cout<< "-------- pointer to tracker hit =  " << vecHits.at(iHitOnEl) << std::endl;
	  notUsedHitsVec->addElement( vecHits.at(iHitOnEl) );
	}//end loop on hits on each det element
      }//end loop on map detEl <--> vector of hits on the detEl
    }//end loops on vector of maps - one for each subdetector
                        
    evt->addCollection( notUsedHitsVec , _output_not_used_col_name ) ;
    
    //delete notUsedHitsVec;

    /////////////////////////////////////////////////////////////////

  }// track collection no empty  
  
  ++_n_evt ;
  
  //cout << " event " << _n_evt << std::endl ;



  
}



void ExtrToTracker::check( LCEvent * evt ) { 
  // nothing to check here - could be used to fill checkplots in reconstruction processor
}


void ExtrToTracker::end(){ 
  
  streamlog_out(DEBUG) << "ExtrToTracker::end()  " << name() 
		       << " processed " << _n_evt << " events in " << _n_run << " runs "
		       << std::endl ;

  streamlog_out(DEBUG4) << " SIT hits considered for track-hit association " << TotalSITHits << " how many of them were matched and fitted successfully ? " << SITHitsFitted << " for how many the fit failed ? " << SITHitsNonFitted << std::endl ;



  
}









LCCollection* ExtrToTracker::GetCollection( LCEvent * evt, std::string colName ){
  
  LCCollection* col = NULL;
  
  // int nElements = 0;
  
  try{
    col = evt->getCollection( colName.c_str() ) ;
    //int nElements = col->getNumberOfElements()  ;
    streamlog_out( DEBUG3 ) << " --> " << colName.c_str() << " track collection found in event = " << col << " number of elements " << col->getNumberOfElements() << std::endl;
  }
  catch(DataNotAvailableException &e){
    streamlog_out( DEBUG3 ) << " --> " << colName.c_str() <<  " collection absent in event" << std::endl;     
  }
  
  return col; 
  
}





int ExtrToTracker::FitInit2( Track* track, MarlinTrk::IMarlinTrack* _marlinTrk ){


  //EVENT::FloatVec covMatrix = track->getCovMatrix();
  
  
  TrackStateImpl trackState( TrackState::AtOther, 
			     track->getD0(), 
			     track->getPhi(), 
			     track->getOmega(), 
			     track->getZ0(), 
			     track->getTanLambda(), 
			     track->getCovMatrix(), 
			     track->getReferencePoint()
			     ) ;
  

  //_marlinTrk->initialise( trackState, _bField, IMarlinTrack::backward ) ;
  _marlinTrk->initialise( trackState, _bField, IMarlinTrack::forward ) ;

  
  return IMarlinTrack::success ;   
  

}







void ExtrToTracker::fillVecSubdet(lcio::LCEvent*& evt){


  //_vecSubdetNLayers.clear();
  //_vecSubdetID.clear();
  _vecDigiHitsCol.clear();

  for (size_t idet=0; idet<_vecSubdetName.size(); idet++){
       
    streamlog_out(DEBUG4) << "idet = " << idet << std::endl;   
 
    // int nSubdetLayers = 0;
    // int idSubdet = 0;

    if ( _vecDigiHits.at(idet)!="" ){ 
	 	
      LCCollection* colDigi = 0 ;
      try{
	colDigi = evt->getCollection( _vecDigiHits.at(idet) ) ;
      }catch(DataNotAvailableException &e){
	streamlog_out(DEBUG4) << "Collection " << _vecDigiHits.at(idet).c_str() << " is unavailable " << std::endl;
      }	

     //  if( colDigi != 0 ){ 
     // 	CellIDDecoder<TrackerHitPlane> cellid_decoder(colDigi);
     // 	int last_element = colDigi->getNumberOfElements()-1;
     // 	streamlog_out(DEBUG2) << "last_element = " << last_element << std::endl;
     // 	if (last_element>=0) {
     // 	  _vecDigiHitsCol.push_back(colDigi);
     // 	  TrackerHitPlane* hit_helper2 = dynamic_cast<TrackerHitPlane*>( colDigi->getElementAt(last_element) ) ;
     // 	  nSubdetLayers = cellid_decoder( hit_helper2 )["layer"] + 1; //from 0 to n-1
     // 	  idSubdet = cellid_decoder( hit_helper2 )["subdet"]; 
     // 	}
     // 	else   _vecDigiHitsCol.push_back(0); 
     // }//end colDigi!=0 
     //  else {
	_vecDigiHitsCol.push_back(colDigi);
      // }
      // streamlog_out(DEBUG2) << "_vecDigiHitsCol.back() = " << _vecDigiHitsCol.back() << std::endl;
      
    }// diginame !=0


    //NEW LINE
    else _vecDigiHitsCol.push_back(0); 
    ///////


    //_vecSubdetNLayers.push_back(nSubdetLayers);
    //_vecSubdetID.push_back(idSubdet);

    // streamlog_out(DEBUG4) << "nSubdetLayers = " << nSubdetLayers << std::endl;
    // streamlog_out(DEBUG4) << "idSubdet = " << idSubdet << std::endl;

  }//end loop on subdetectors
	  
}//end 






TrackerHitPlane* ExtrToTracker::getSiHit(std::vector<TrackerHitPlane* >& hitsOnDetEl, MarlinTrk::IMarlinTrack*& marlin_trk){
  
  double min = 9999999.;
  double testChi2=0.;
  size_t nHitsOnDetEl = hitsOnDetEl.size();
  int index = -1; //index of the selected hits

  streamlog_out(DEBUG2) << "-- number of hits on the same detector element: " << nHitsOnDetEl << std::endl ;

  if (nHitsOnDetEl==0) return 0;

  for(size_t i=0; i<nHitsOnDetEl; i++){
    //if ( marlin_trk->testChi2Increment(hitsOnDetEl.at(i), testChi2) == MarlinTrk::IMarlinTrack::success ) {..} // do not do this the testChi2 internally call the addandfir setting as max acceptable chi2 a very small number to be sure to never add the the hit to the fit (so it always fails)
    marlin_trk->testChi2Increment(hitsOnDetEl.at(i), testChi2);
    streamlog_out(DEBUG2) << "-- testChi2: " << testChi2 << std::endl ;
    if (min>testChi2 && testChi2>0) {
      min = testChi2;
      index = i;
    } //end min chi2
  }//end loop on hits on same detector element

  streamlog_out(DEBUG2) << "-- index of the selected hit: " << index << std::endl ;
    
  if (index == -1) return 0;
  else {
    TrackerHitPlane* selectedHit = dynamic_cast<TrackerHitPlane*>( hitsOnDetEl.at(index) ) ;

    if (nHitsOnDetEl>1) std::iter_swap(hitsOnDetEl.begin()+index,hitsOnDetEl.begin()+nHitsOnDetEl-1); 
    hitsOnDetEl.pop_back();
    //hitsOnDetEl.erase(hitsOnDetEl.begin()+index);


    return selectedHit;
  }

}//end getSiHit







TrackerHitPlane* ExtrToTracker::getSiHit(std::vector<int >& vecElID, std::map<int , std::vector<TrackerHitPlane* > >& mapElHits, MarlinTrk::IMarlinTrack*& marlin_trk){
  
  double min = 9999999.;
  double testChi2=0.;
  size_t nElID = vecElID.size();
  int indexel = -1; //element index (in vecElID) of the selected hits
  int index = -1; //index of the selected hits

  for(size_t ie=0; ie<nElID; ie++){
    
    int elID = vecElID.at(ie);
    size_t nHitsOnDetEl = 0;
    if (mapElHits.count(elID)>0) {
      nHitsOnDetEl = mapElHits[elID].size();
    }

    // streamlog_out(MESSAGE2) << "-- elID at index = "<< ie <<" / " << nElID << " : " << vecElID.at(ie) << std::endl ;
    streamlog_out(DEBUG3) << "-- number of hits on the same detector element: " << nHitsOnDetEl << std::endl ;


    for(size_t i=0; i<nHitsOnDetEl; i++){
      marlin_trk->testChi2Increment(mapElHits[elID].at(i), testChi2);
      // streamlog_out(MESSAGE2) << "-- trackerhit at index = " << i << " / "<< nHitsOnDetEl << std::endl ;
      // streamlog_out(MESSAGE2) << "-- testChi2: " << testChi2 << std::endl ;
      if (min>testChi2 && testChi2>0) {
	min = testChi2;
	indexel = elID;
	index = i;	
      }
    }//end loop on hits on the same elID

  }//end loop on elIDs


  if (index == -1 || indexel == -1) return 0;
  else {

    streamlog_out(DEBUG3) << "-- hit added " << std::endl ;
    // if ( vecElID.at(0) != indexel) streamlog_out(MESSAGE2) << "-- but not from the first elementID " << std::endl ;


    TrackerHitPlane* selectedHit = dynamic_cast<TrackerHitPlane*>( mapElHits[indexel].at(index) ) ;

    int nHitsOnSelectedEl = mapElHits[indexel].size();
    if (nHitsOnSelectedEl>1) std::iter_swap(mapElHits[indexel].begin()+index,mapElHits[indexel].begin()+nHitsOnSelectedEl-1); 
    mapElHits[indexel].pop_back();
    ////(mapElHits[indexel].erase((mapElHits[indexel].begin()+index);
    
    return selectedHit;
  }

  return 0;

}//end getSiHit




void ExtrToTracker::fillMapElHits(std::vector<LCCollection* >& vecHitCol, std::vector<std::map<int , std::vector<TrackerHitPlane* > > >& vecMaps){


  //fill map (el - vector of hits) for each subdtector

  vecMaps.clear();

  for(size_t icol=0; icol<vecHitCol.size(); icol++){

    std::map<int , std::vector<TrackerHitPlane* > > map_el_hits;

    if(vecHitCol.at(icol)!=NULL){

      int nhits = vecHitCol.at(icol)->getNumberOfElements();
      streamlog_out(DEBUG2) << "  nhits = "<< nhits << std::endl ; 


      for(int ihit=0; ihit<nhits; ihit++){

	TrackerHitPlane* hit = dynamic_cast<TrackerHitPlane*>( vecHitCol.at(icol)->getElementAt(ihit) );
  
	int cellID0 = hit->getCellID0();
	UTIL::BitField64 encoder0(  lcio::ILDCellID0::encoder_string );  //do not change it, code will not work with a different encoder	    
	encoder0.reset();  // reset to 0
	encoder0.setValue(cellID0);
	int hitElID = encoder0.lowWord();  

	map_el_hits[hitElID].push_back(hit);
	
      }//end loop on hits

    }//collection not empty

    vecMaps.push_back(map_el_hits);

  }//end loop on collections of hits


}//end fillMapElHits








void ExtrToTracker::getGeoInfo(){
  

  _vecSubdetID.clear();
  _vecSubdetNLayers.clear();
  _vecMapLayerNModules.clear();

  DD4hep::Geometry::LCDD & lcdd = DD4hep::Geometry::LCDD::getInstance();
     
  //alternative way 
  // const std::vector< DD4hep::Geometry::DetElement>& barrelDets = DD4hep::Geometry::DetectorSelector(lcdd).detectors(  ( DD4hep::DetType::TRACKER | DD4hep::DetType::BARREL )) ;
  
  // streamlog_out( MESSAGE2 ) << " --- flag = " << (DD4hep::DetType::TRACKER | DD4hep::DetType::BARREL) <<std::endl;
  // streamlog_out( MESSAGE2 ) << " --- number of barrel detectors = " << barrelDets.size() <<std::endl;


  const double pos[3]={0,0,0}; 
  double bFieldVec[3]={0,0,0}; 
  lcdd.field().magneticField(pos,bFieldVec); // get the magnetic field vector from DD4hep
  _bField = bFieldVec[2]/dd4hep::tesla; // z component at (0,0,0)


  streamlog_out( DEBUG2 ) << " - _bField = " << _bField <<std::endl;


  for (size_t i=0; i<_vecSubdetName.size(); i++){

    int detID = 0;
    int nlayers = 0;
    std::map<int , int > map_layerID_nmodules;
    
    try{

  
	const DD4hep::Geometry::DetElement& theDetector = lcdd.detector(_vecSubdetName.at(i));
	
	detID = theDetector.id();
	streamlog_out( DEBUG2 ) << " --- subdet: " << _vecSubdetName.at(i) << " - id = " << detID <<std::endl;


	if (_vecSubdetName.at(i).find("Barrel") != std::string::npos) {

	  DD4hep::DDRec::ZPlanarData * theExtension = 0;
	  theExtension = theDetector.extension<DD4hep::DDRec::ZPlanarData>();

	  nlayers = theExtension->layers.size();

	  streamlog_out( DEBUG2 ) << " - n layers = " << nlayers <<std::endl;


	  for(int il=0; il<nlayers; il++){

	    int nmodules = theExtension->layers.at(il).ladderNumber;
            
	    streamlog_out( DEBUG2 ) << " --- n modules = " << nmodules << "  per layer " << il <<std::endl;

	    map_layerID_nmodules.insert( std::pair<int,int>(il,nmodules) );

	  }

	}//end barrel type
	else {

	  DD4hep::DDRec::ZDiskPetalsData * theExtension = 0;  
	  theExtension = theDetector.extension<DD4hep::DDRec::ZDiskPetalsData>();
            
	  nlayers = theExtension->layers.size();

	  streamlog_out( DEBUG2 ) << " - n layers = " << nlayers <<std::endl;


	  for(int il=0; il<nlayers; il++){

	    int nmodules = theExtension->layers.at(il).petalNumber;
            
	    streamlog_out( DEBUG2 ) << " --- n modules = " << nmodules << "  per layer " << il <<std::endl;

	    map_layerID_nmodules.insert( std::pair<int,int>(il,nmodules) );

	  }

	  
	}//end endcap type

    } catch (std::runtime_error &exception){
            
      streamlog_out(WARNING) << "ExtrToTracker::getGeoInfo - exception in retriving number of modules per layer for subdetector : "<< _vecSubdetName.at(i) <<" : " << exception.what() << std::endl;

    }

    _vecSubdetID.push_back(detID);
    _vecSubdetNLayers.push_back(nlayers);
    _vecMapLayerNModules.push_back(map_layerID_nmodules);


  }//end loop on subdetector names
 

}//end getGeoInfo





void ExtrToTracker::getNeighbours(int elID, std::vector<int >& vecIDs, std::string cellIDEcoding, std::map<int , int > mapLayerNModules){

  UTIL::BitField64 cellid_decoder( cellIDEcoding ) ;

  cellid_decoder.setValue( elID ) ;

  streamlog_out(DEBUG1) << "-- cellid_decoder.valueString() = " << cellid_decoder.valueString() <<std::endl;

  int newmodule = 0; //stave
  int newsensor = 0;

  int subdet = cellid_decoder["subdet"].value();
  int layer = cellid_decoder["layer"].value();
  int module = cellid_decoder["module"].value(); //stave
  int sensor = cellid_decoder["sensor"].value();

  streamlog_out(DEBUG2) << "-- subdet = " << subdet <<std::endl;
  streamlog_out(DEBUG2) << "-- layer = " << layer <<std::endl;
  streamlog_out(DEBUG2) << "-- module (stave) = " << module <<std::endl;
  streamlog_out(DEBUG2) << "-- sensor = " << sensor <<std::endl;


  int lastModule = mapLayerNModules[layer]-1;


  int steps[] = {-1, 0, +1};
  std::vector<int > phiSteps (steps, steps + sizeof(steps) / sizeof(int) ); // add sensors on -1 and +1 staves around the current one
  std::vector<int > zSteps (steps, steps + sizeof(steps) / sizeof(int) ); // add sensors -1 before and +1 after the current one on the same stave 
  
  for(size_t ip=0; ip<phiSteps.size(); ip++) { 
    for(size_t iz=0; iz<zSteps.size(); iz++) { 
      if (phiSteps.at(ip)!=0 || zSteps.at(iz)!=0) { // if it is not the sensor itself
		
	newmodule = module + phiSteps.at(ip);
	newsensor = sensor + zSteps.at(iz);

	//make the module a cycle (tis is actually true for the barrels but not for the disks)
	if(lastModule>0) { //check to be removed in future but at the moment the endcap data structure is not correctly filled
	  if (newmodule == (lastModule+1)) newmodule = 0;
	  else if (newmodule==-1) newmodule = lastModule;
	}

	if(newsensor>0){

	  cellid_decoder.reset();
    	 	  
	  try{

	    cellid_decoder[lcio::ILDCellID0::subdet] = subdet;
	    cellid_decoder[lcio::ILDCellID0::layer]  = layer;   
	    cellid_decoder[lcio::ILDCellID0::module]  = newmodule;   
	    cellid_decoder[lcio::ILDCellID0::sensor]  = newsensor; 

	    int newElID = cellid_decoder.lowWord();  

	    streamlog_out(DEBUG2) << "-- new element ID = " << newElID <<std::endl;
	    vecIDs.push_back(newElID);

	  } catch(lcio::Exception &e){

	    streamlog_out(DEBUG2) << "BitFieldValue out of range - sensor was at the edge of the module of 1 make it out of range - the -1 one case it is cut out by asking for a sensor number > 0" << std::endl;

	  }	
	  
	}//sensor number >0

      }//end not same elementID we started with
    }//end loop on z    
  }//end loop on phi

  return;

}//end getNeighbours





