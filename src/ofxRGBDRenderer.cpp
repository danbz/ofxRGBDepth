/*
 *  ofxRGBDRenderer.cpp
 *  ofxRGBDepthCaptureOpenNI
 *
 *  Created by Jim on 12/17/11.
 *  Copyright 2011 FlightPhase. All rights reserved.
 *
 */

#include "ofxRGBDRenderer.h"
#include <set>

ofxRGBDRenderer::ofxRGBDRenderer(){
	//come up with better names
	xmult = 0;
	ymult = 0;
	
	edgeCull = 4000;
	simplify = 1;

	farClip = 6000;
	ZFuzz = 0;
    meshRotate = ofVec3f(0,0,0);
    
    calculateNormals = false;
    
	hasDepthImage = false;
	hasRGBImage = false;

    shaderBound = false;
    rendererBound = false;
    

	mirror = false;
	calibrationSetup = false;
    
    reloadShader();
    setSimplification(1);
    
    hasVerts = false;
    forceUndistortOff = false;
    addColors = false;
    calculateTextureCoordinates = false;
}

ofxRGBDRenderer::~ofxRGBDRenderer(){

}

bool ofxRGBDRenderer::setup(string calibrationDirectory){
	
	if(!ofDirectory(calibrationDirectory).exists()){
		ofLogError("ofxRGBDRenderer --- Calibration directory doesn't exist: " + calibrationDirectory);
		return false;
	}
	
	depthCalibration.load(calibrationDirectory+"/depthCalib.yml");
	rgbCalibration.load(calibrationDirectory+"/rgbCalib.yml");
	
	loadMat(rotationDepthToRGB, calibrationDirectory+"/rotationDepthToRGB.yml");
	loadMat(translationDepthToRGB, calibrationDirectory+"/translationDepthToRGB.yml");
    
    depthToRGBView = ofxCv::makeMatrix(rotationDepthToRGB, translationDepthToRGB);

    ofPushView();
    rgbCalibration.getDistortedIntrinsics().loadProjectionMatrix();
    glGetFloatv(GL_PROJECTION_MATRIX, rgbProjection.getPtr());
    ofPopView();

    calibrationSetup = true;
	return true;
}

void ofxRGBDRenderer::setSimplification(int level){
	simplify = level;
	if (simplify <= 0) {
		simplify = 1;
	}
	else if(simplify > 8){
		simplify = 8;
	}

	baseIndeces.clear();
	int w = 640 / simplify;
	int h = 480 / simplify;
	for (int y = 0; y < h-1; y++){
		for (int x=0; x < w-1; x++){
			ofIndexType a,b,c;
			a = x+y*w;
			b = x+(y+1)*w;
			c = (x+1)+y*w;
			baseIndeces.push_back(a);
			baseIndeces.push_back(b);
			baseIndeces.push_back(c);
			
			a = (x+1)+y*w;
			b = x+(y+1)*w;
			c = (x+1)+(y+1)*w;
			baseIndeces.push_back(a);
			baseIndeces.push_back(b);
			baseIndeces.push_back(c);
		}
	}		
	
	indexMap.clear();
	for (int y = 0; y < 480; y+=simplify){
		for (int x=0; x < 640; x+=simplify){
			IndexMap m;
            m.valid = false;
			indexMap.push_back(m);
		}
	}

	simpleMesh.clearVertices();
	for (int y = 0; y < 640; y++){
		for (int x=0; x < 480; x++){
			simpleMesh.addVertex(ofVec3f(0,0,0));
		}
	}
    
    
    if(addColors){
        simpleMesh.clearColors();
        for (int y = 0; y < 640; y++){
            for (int x=0; x < 480; x++){
                simpleMesh.addColor(ofFloatColor(1.0,1.0,1.0,1.0));
            }
        }        
    }
    
    if(calculateTextureCoordinates){
        simpleMesh.clearTexCoords();
        for (int y = 0; y < 640; y++){
            for (int x=0; x < 480; x++){
                simpleMesh.addTexCoord(ofVec2f(0,0));
            }
        }        
    }
    
	//cout << "AFTER SETUP base indeces? " << baseIndeces.size() << " index map? " << indexMap.size() << endl;
}

//-----------------------------------------------
int ofxRGBDRenderer::getSimplification(){
	return simplify;
}

//-----------------------------------------------
void ofxRGBDRenderer::setRGBTexture(ofBaseHasTexture& rgbImage) {
	currentRGBImage = &rgbImage;
	hasRGBImage = true;
}

ofBaseHasTexture& ofxRGBDRenderer::getRGBTexture() {
    return *currentRGBImage;
}

void ofxRGBDRenderer::setDepthImage(ofShortPixels& pix){
	currentDepthImage.setFromPixels(pix);
	if(!undistortedDepthImage.isAllocated()){
		undistortedDepthImage.allocate(640,480,OF_IMAGE_GRAYSCALE);
	}
	hasDepthImage = true;
}

void ofxRGBDRenderer::setDepthImage(unsigned short* depthPixelsRaw){
	currentDepthImage.setFromPixels(depthPixelsRaw, 640,480, OF_IMAGE_GRAYSCALE);
	if(!undistortedDepthImage.isAllocated()){
		undistortedDepthImage.allocate(640,480,OF_IMAGE_GRAYSCALE);
	}
	hasDepthImage = true;
}

Calibration& ofxRGBDRenderer::getDepthCalibration(){
	return depthCalibration;
}

Calibration& ofxRGBDRenderer::getRGBCalibration(){
	return rgbCalibration;
}

void ofxRGBDRenderer::update(){
	
    
	if(!hasDepthImage){
     	ofLogError("ofxRGBDRenderer::update() -- no depth image");
        return;
    }

    if(!calibrationSetup && hasRGBImage){
     	ofLogError("ofxRGBDRenderer::update() -- no calibration for RGB Image");
        return;
    }

	bool debug = false;
	
	int w = 640;
	int h = 480;
	
	int start = ofGetElapsedTimeMillis();

	Point2d fov = depthCalibration.getUndistortedIntrinsics().getFov();
	
	float fx = tanf(ofDegToRad(fov.x) / 2) * 2;
	float fy = tanf(ofDegToRad(fov.y) / 2) * 2;
	
	Point2d principalPoint = depthCalibration.getUndistortedIntrinsics().getPrincipalPoint();
	cv::Size imageSize = depthCalibration.getUndistortedIntrinsics().getImageSize();
	

    if(!forceUndistortOff){
        depthCalibration.undistort( toCv(currentDepthImage), toCv(undistortedDepthImage), CV_INTER_NN);
    }
    else {
        undistortedDepthImage = currentDepthImage;
    }
	
    //start
	int imageIndex = 0;
	int vertexIndex = 0;
	float xReal,yReal;
    int indexPointer = 0;
    int vertexPointer = 0;
    hasVerts = false;
	unsigned short* ptr = undistortedDepthImage.getPixels();
	for(int y = 0; y < h; y += simplify) {
		for(int x = 0; x < w; x += simplify) {
            vertexPointer = y*w+x;
			unsigned short z = undistortedDepthImage.getPixels()[y*w+x];
			IndexMap& indx = indexMap[indexPointer];
			if(z != 0 && z < farClip){
				xReal = (((float)x - principalPoint.x) / imageSize.width) * z * fx;
				yReal = (((float)y - principalPoint.y) / imageSize.height) * z * fy;
                indx.vertexIndex = vertexPointer;
				indx.valid = true;
                simpleMesh.setVertex(vertexPointer, ofVec3f(xReal, yReal, z));
                hasVerts = true;
			}
			else {
				indx.valid = false;
			}
            indexPointer++;
		}
	}
    //end
    
    if(debug && !hasVerts) cout << "warning no verts with far clip " << farClip << endl; 
	if(debug) cout << "unprojection " << simpleMesh.getVertices().size() << " took " << ofGetElapsedTimeMillis() - start << endl;
	
	simpleMesh.clearIndices();
	set<ofIndexType> calculatedNormals;
	start = ofGetElapsedTimeMillis();
    if(calculateNormals){
		simpleMesh.getNormals().resize(simpleMesh.getVertices().size());
	}
    
	for(int i = 0; i < baseIndeces.size(); i+=3){
		if(indexMap[baseIndeces[i]].valid &&
		   indexMap[baseIndeces[i+1]].valid &&
		   indexMap[baseIndeces[i+2]].valid){
			
			ofVec3f a,b,c;
			a = simpleMesh.getVertices()[indexMap[baseIndeces[i]].vertexIndex]; 
			b = simpleMesh.getVertices()[indexMap[baseIndeces[i+1]].vertexIndex]; 
			c = simpleMesh.getVertices()[indexMap[baseIndeces[i+2]].vertexIndex]; 
			if(fabs(a.z - b.z) < edgeCull && fabs(a.z - c.z) < edgeCull){
				simpleMesh.addTriangle(indexMap[baseIndeces[i]].vertexIndex, 
									   indexMap[baseIndeces[i+1]].vertexIndex,
									   indexMap[baseIndeces[i+2]].vertexIndex);
				
				if(calculateNormals && calculatedNormals.find(indexMap[baseIndeces[i]].vertexIndex) == calculatedNormals.end()){
					//calculate normal
					simpleMesh.setNormal(indexMap[baseIndeces[i]].vertexIndex, (b-a).getCrossed(b-c).getNormalized());
					calculatedNormals.insert(indexMap[baseIndeces[i]].vertexIndex);
				}
			}
		}
	}
	
	if(debug) cout << "indexing  " << simpleMesh.getIndices().size() << " took " << ofGetElapsedTimeMillis() - start << endl;
    
    //normally this is done in the shader, but sometimes we want to have them on the CPU for doing special processing
	if(calculateTextureCoordinates){
        generateTextureCoordinates();
//		if(debug) cout << "gen tex coords took " << (ofGetElapsedTimeMillis() - start) << endl;
	}
}

void ofxRGBDRenderer::generateTextureCoordinates(){
    if(!calibrationSetup){
        ofLogError("ofxRGBDRenderer::generateTextureCoordinates -- no calibration set up");
        return;
    }
    
    if(!simpleMesh.hasTexCoords()){
        for (int y = 0; y < 640*480; y++){
            simpleMesh.addTexCoord(ofVec2f(0,0));
        } 
    }
    
    Mat pcMat = Mat(toCv(simpleMesh));		
    imagePoints.clear();
       
   projectPoints(pcMat,
                 rotationDepthToRGB, translationDepthToRGB,
                 rgbCalibration.getDistortedIntrinsics().getCameraMatrix(),
                 rgbCalibration.getDistCoeffs(),
                 imagePoints);
    
    for(int i = 0; i < imagePoints.size(); i++) {
	    //TODO account for fudge factor that the shader listens to
        simpleMesh.setTexCoord(i, ofVec2f(imagePoints[i].x, imagePoints[i].y));			
	}
}

bool ofxRGBDRenderer::isVertexValid(int index){
    return indexMap[index].valid;
}

int ofxRGBDRenderer::vertexIndex(int sequenceIndex){
	return indexMap[sequenceIndex].vertexIndex;    
}

int ofxRGBDRenderer::getTotalPoints(){
	return indexMap.size();
}
ofMesh& ofxRGBDRenderer::getMesh(){
	return simpleMesh;
}

void ofxRGBDRenderer::reloadShader(){
    shader.load("shaders/unproject");
}

bool ofxRGBDRenderer::bindRenderer(bool useShader){
//	glEnable(GL_DEPTH_TEST);
	if(!hasDepthImage){
     	ofLogError("ofxRGBDRenderer::update() -- no depth image");
        return false;
    }
    
    if(!calibrationSetup){
     	ofLogError("ofxRGBDRenderer::update() -- no calibration");
        return false;
    }
	
    ofPushMatrix();
    ofScale(1, -1, 1);
    if(mirror){
	    ofScale(-1, 1, 1);    
    }
    ofRotate(meshRotate.x,1,0,0);
    ofRotate(meshRotate.y,0,1,0);
    ofRotate(meshRotate.z,0,0,1);
	

//    ofPushStyle();
//    ofPushMatrix();
//    glMultMatrixf(rgbMatrix.getInverse().getPtr());
//    ofNoFill();
//    ofBox(2.0f);
//    ofPopMatrix();
//    ofPopStyle();

//    if(ofGetKeyPressed('v'))
//        cout << "view " <<depthToRGBView << endl;
//    else if (ofGetKeyPressed('p'))
//        cout << "projection " << rgbProjection << endl;
	//cout << rgbMatrix << endl;
    
	if(hasRGBImage){
		currentRGBImage->getTextureReference().bind();
        if(useShader){
            shader.begin();	
            setupProjectionUniforms(shader);
            shaderBound = true;
             
        }
	}
    rendererBound = true;
    return true;
}


void ofxRGBDRenderer::unbindRenderer(){
    
    if(!rendererBound){
        ofLogError("ofxRGBDRenderer::unbindRenderer -- called without renderer bound");
     	return;   
    }
    
    if(hasRGBImage){
		currentRGBImage->getTextureReference().unbind();
        if(shaderBound){
            restortProjection();
            shader.end();
            shaderBound = false;
        }
	}
    
	
	ofPopMatrix();
    rendererBound = false;
}


void ofxRGBDRenderer::setupProjectionUniforms(ofShader& theShader){

    rgbMatrix = (depthToRGBView * rgbProjection);
    ofVec2f dims = ofVec2f(currentRGBImage->getTextureReference().getWidth(), 
                           currentRGBImage->getTextureReference().getHeight());
    theShader.setUniform2f("fudge", xmult, ymult);
    theShader.setUniform2f("dim", dims.x, dims.y);
    
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadMatrixf(rgbMatrix.getPtr());
    glMatrixMode(GL_MODELVIEW);      
}

void ofxRGBDRenderer::restortProjection(){
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);   
}

void ofxRGBDRenderer::drawMesh(bool useShader) {
	if(!hasVerts) return;
    
    if(bindRenderer(useShader)){
		simpleMesh.drawFaces();
        unbindRenderer();
    }
}

void ofxRGBDRenderer::drawPointCloud(bool useShader) {
	if(!hasVerts) return;
    if(bindRenderer(useShader)){
	    simpleMesh.drawVertices();
        unbindRenderer();
    }
    
}

void ofxRGBDRenderer::drawWireFrame(bool useShader) {
    if(!hasVerts) return;
	if(bindRenderer(useShader)){
		simpleMesh.drawWireframe();
        unbindRenderer();
    }
	
}

ofTexture& ofxRGBDRenderer::getTextureReference(){
	return currentRGBImage->getTextureReference();
}
