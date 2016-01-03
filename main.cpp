//
//  main.cpp
//  ModelWiki
//
//  Created by XuXiang on 15/12/27.
//  Copyright © 2015年 geohey. All rights reserved.
//

#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <regex>

#include <curl/curl.h>
#include "ogrsf_frmts.h"

#define COUNTRY_CODE_LINK "https://en.wikipedia.org/wiki/ISO_3166-1_alpha-2#IN"
#define LINKS2COUNTRY_LINK "https://en.wikipedia.org/wiki/Special:WhatLinksHere"

struct MemoryStruct {
    char *memory;
    size_t size;
};

void* myrealloc(void *ptr, size_t size) {
    if(ptr) {
        return realloc(ptr, size);
    } else {
        return malloc(size);
    }
}

size_t curl_callback(void *ptr, size_t size, size_t nmemb, void *data) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)data;
    mem->memory = (char *)myrealloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory) {
        memcpy(&(mem->memory[mem->size]), ptr, realsize);
        mem->size += realsize;
        mem->memory[mem->size] = 0;
    }
    return realsize;
}

bool curl_get(std::string url, std::string& data) {
    struct MemoryStruct chunk;
    chunk.memory=NULL;
    chunk.size = 0;
    
    CURL* curl = curl_easy_init();
    
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        CURLcode res = curl_easy_perform(curl);
        
        if(res != CURLE_OK) {
            curl_easy_cleanup(curl);
            
            // clean
            if (chunk.memory) {
                free(chunk.memory);
            }
            
            return false;
        }
        
        curl_easy_cleanup(curl);
        data = std::string(chunk.memory, chunk.size);
        
        // clean
        if (chunk.memory) {
            free(chunk.memory);
        }
        
        return true;
    } else {
        // clean
        if (chunk.memory) {
            free(chunk.memory);
        }
        
        return false;
    }
}

class Coord {
public:
    Coord() {}
    Coord(double x, double y): x_(x), y_(y) {}
    ~Coord() {}
    
    double x_;
    double y_;
};

class Country {
public:
    Country(std::string code, std::string name = "", std::string url = ""):
    code_(code), name_(name), url_(url) {
    }
    
    ~Country() {}
    
    inline std::string get_code() { return code_; }
    inline std::string get_name() { return name_; }
    inline std::string get_url() { return url_; }
    
    inline void set_code(std::string code) { code_ = code; }
    inline void set_name(std::string name) { name_ = name; }
    inline void set_url(std::string url) { url_ = url; }

    inline int AddLink(int index) {
        links_.emplace_back(index);
        return static_cast<int>(links_.size());
    }
    
    inline std::vector<int>& get_links() { return links_; }
    
private:
    std::string code_;
    std::string name_;
    std::string url_;
    
    // coordinate
    Coord loc_;
    
    std::vector<int> links_;
};

class RelationModel {
public:
    RelationModel() {
        OGRRegisterAll();
    }
    
    ~RelationModel() {
    }
    
    enum SearchType {
        CODE, NAME, URL
    };

    int Search(std::string key, SearchType type) {
        switch (type) {
            case CODE:
                if (code_countries_.find(key) == code_countries_.end()) {
                    return -1;
                } else {
                    return code_countries_.at(key);
                }
                break;
            case NAME:
                if (name_countries_.find(key) == name_countries_.end()) {
                    return -1;
                } else {
                    return name_countries_.at(key);
                }
            case URL:
                if (url_countries_.find(key) == url_countries_.end()) {
                    return -1;
                } else {
                    return url_countries_.at(key);
                }
            default:
                return -1;
                break;
        }
    }
    
    inline Country& GetCountry(int index) { return countries_[index]; }
    
    bool LoadCodeLocation() {
        OGRDataSource* datsource = OGRSFDriverRegistrar::Open("admin/world_admin_point.shp", FALSE);
        if (datsource == NULL) {
            return false;
        }
        OGRLayer* layer = datsource->GetLayer(0);
        if (layer == NULL) {
            return false;
        }
        
        int count = 0;
        OGRFeature* feature = layer->GetNextFeature();
         while (feature != NULL) {
             std::string code = feature->GetFieldAsString("ISO2");
             if (!std::isupper(code[0]) || !std::isupper(code[1])) {
                 std::cout << "code: " << code << " is invalid!\n";
                 
                 OGRFeature::DestroyFeature (feature);
                 feature = layer->GetNextFeature();
                 
                 continue;
             }

             OGRGeometry* geometry = feature->GetGeometryRef();
             if (geometry != NULL) {
                 OGRPoint* point = (OGRPoint*)geometry;
                 Coord pt(point->getX(), point->getY());
                 
                 locations_.emplace_back(pt);
                 this->code_locations_.emplace(std::make_pair(code, count++));
                 
                 OGRFeature::DestroyFeature (feature);
                 feature = layer->GetNextFeature();
             }
         }
        if (datsource != NULL) {
            OGRDataSource::DestroyDataSource(datsource);
        }
        
        std::cout << "load " << count << " countries code and its coordinates\n";
        return true;
    }
    
    bool InitCountryList() {
        std::cout << "load country list ...\n";
        
        std::string content;
        if (!curl_get(COUNTRY_CODE_LINK, content)) {
            return false;
        }
        
        // regex search
        std::regex expression("<td id=\"[A-Z]{2}\"><span style=\"[^\"]*\">[A-Z]{2}</span></td>\n<td><a href=\"[^\"]*\" title=\"[^\"]*\">.*</a></td>");
        
        std::sregex_iterator iter(content.cbegin(), content.cend(), expression);
        std::sregex_iterator iter_end;
        int count = 0;
        for (; iter != iter_end; ++iter) {
            std::string searched_string = (*iter)[0].str();
            std::string code, url, name;
            
            // country code
            code = searched_string.substr(8, 2);
            if (!std::isupper(code[0]) || !std::isupper(code[1])) {
                std::cout << "code: " << code << " is invalid!\n";
            }
            // std::cout << "code: " << code << std::endl;
            
            // country url
            std::regex url_expresion("href=\"[^\"]*\" ");
            std::smatch match;
            if (std::regex_search (searched_string, match, url_expresion)) {
                std::string s = match[0].str();
                url = s.substr(6, s.length() - 8);
                //std::cout << "url: " << url << std::endl;
            }
            
            // country name
            std::regex name_expresion("title=\"[^\"]*\"");
            if (std::regex_search (searched_string, match, name_expresion)) {
                std::string s = match[0].str();
                name = s.substr(7, s.length() - 8);
                //std::cout << "name: " << name << std::endl;
            }
            
            // construct country
            Country country(code, name, url);
            this->countries_.push_back(country);
            this->code_countries_.emplace(std::make_pair(code, count));
            this->name_countries_.emplace(std::make_pair(name, count));
            this->url_countries_.emplace(std::make_pair(url, count++));
        }
        std::cout << "country list size: " << count << std::endl;
        return true;
    }
    
    bool ModelCountryRelation() {
        std::cout << "\n\nFind country relations ...\n";
        for (size_t i = 0; i < countries_.size(); ++i) {
        //for (size_t i = 45; i < 46; ++i) { // china
            Country& country = countries_[i];
            std::cout << "\nProcess country #" << i << ", " << country.get_name() << std::endl;
            
            // get page with maximal links list
            std::string links_url = LINKS2COUNTRY_LINK + country.get_url().substr(5) + "?limit=5000";
            std::string links_page;
            bool has_next_page = false;
            int count = 0;
            do {
                std::cout << "Get links page from url: " << links_url << std::endl;
                if (!curl_get(links_url, links_page)) {
                    has_next_page = false;
                    continue;
                }
                
                // regex search country page
                std::regex expression("<li><a href=\"[^\"]*\" title=\"[^\"]*\">.*</a>  ‎ <span class=");
                std::sregex_iterator iter(links_page.cbegin(), links_page.cend(), expression);
                std::sregex_iterator iter_end;
                
                for (; iter != iter_end; ++iter) {
                    std::string searched_string = (*iter)[0].str();
                    
                    std::regex url_expresion("href=\"[^\"]*\" ");
                    std::smatch match;
                    if (std::regex_search (searched_string, match, url_expresion)) {
                        std::string s = match[0].str();
                        std::string href = s.substr(6, s.length() - 8);
                        //std::cout << "url: " << href << std::endl;

                        int index = this->Search(href, URL);
                        if (index != -1) {
                            if (countries_[index].get_url() != href) {
                                std::cout << "Error: index and url mismatched!\n";
                                return false;
                            }
                            
                            count++;
                            country.AddLink(index);
                        }
                    }
                }
                
                // get next page
                std::regex page_expresion("<a href=\"[^\"]*\" title=\"[^\"]*\">next 5,000</a>");
                std::smatch match;
                has_next_page = std::regex_search(links_page, match, page_expresion);
                if (match.size() == 0) {
                    has_next_page = false;
                }

                if (has_next_page) {
                    std::string s = match[0].str();
                
                    // get next page url
                    std::regex url_expresion("from=.*back=[0-9]*");
                    std::smatch match;
                    if (std::regex_search (s, match, url_expresion)) {
                        std::string s = match[0].str();
                        
                        links_url = LINKS2COUNTRY_LINK + country.get_url().substr(5) + "?limit=5000&" + s;
                    }
                }
                
            } while (has_next_page);
            
            std::cout << "add " << count << " links." << std::endl;
            // save every 10 countries, to avoid data loss :)
//            if (i % 10 == 0) {
//                std::cout << "Save ...\n";
//                this->Serialize(country.get_name());
//                
//            }
        }
        return true;
    }
    
    bool Serialize(std::string filename) {
        std::cout << "\n\nSerialize to disk ...\n";
        
        OGRSFDriver* driver = OGRSFDriverRegistrar::GetRegistrar()->GetDriverByName("ESRI Shapefile");
        OGRDataSource* datasource = driver->CreateDataSource(filename.c_str());
        if (datasource == NULL) {
            return false;
        }
        
        OGRSpatialReference sr;
        if (sr.importFromEPSG(3857) != OGRERR_NONE) {
            return false;
        }
        OGRLayer* layer = datasource->CreateLayer("relations", &sr, wkbLineString);
        if (layer == NULL) {
            return false;
        }
        
        OGRFieldDefn from_field("from", OFTString);
        OGRFieldDefn to_field("to", OFTString);
        from_field.SetWidth(254);
        to_field.SetWidth(254);
        if (layer->CreateField(&from_field) != OGRERR_NONE) {
            return false;
        }
        if (layer->CreateField(&to_field) != OGRERR_NONE) {
            return false;
        }
        
        for (size_t i = 0; i < countries_.size(); ++i) {
            Country& country = countries_[i];
            std::vector<int>& links = country.get_links();
            
            for (size_t j = 0; j < links.size(); ++j) {
                Country& link_country =  countries_[links[j]];
                
                std::string from_code = link_country.get_code();
                std::string to_code = country.get_code();
                if (code_locations_.find(from_code) == code_locations_.end()) {
                    std::cout << "Not find code: " << from_code << std::endl;
                }
                if (code_locations_.find(to_code) == code_locations_.end()) {
                    std::cout << "Not find code: " << to_code << std::endl;
                }
                
                if ((from_code != to_code) && (code_locations_.find(from_code) != code_locations_.end()) && (code_locations_.find(to_code) != code_locations_.end())) {
                    std::string from_country = link_country.get_name();
                    std::string to_country = country.get_name();
                    
                    Coord from_loc = locations_[code_locations_.at(from_code)];
                    Coord to_loc = locations_[code_locations_.at(to_code)];
                    
                    OGRFeature* feature = OGRFeature::CreateFeature(layer->GetLayerDefn());
                    feature->SetField("from", from_country.c_str());
                    feature->SetField("to", to_country.c_str());
                    
                    OGRLineString line_string;
                    line_string.addPoint(from_loc.x_, from_loc.y_);
                    line_string.addPoint(to_loc.x_, to_loc.y_);
                    feature->SetGeometry(&line_string);
                    
                    if (layer->CreateFeature(feature) != OGRERR_NONE) {
                        return false;
                    }
                    
                    OGRFeature::DestroyFeature(feature);
                }
            }
        }
        
        if (datasource != NULL) {
            OGRDataSource::DestroyDataSource(datasource);
        }
        
        std::cout << "Serialize done\n";
        return true;
    }
    
    void Validate() {
        for (auto i = code_locations_.begin(); i != code_locations_.end(); ++i) {
            int index = this->Search(i->first, CODE);
            if (index == -1) {
                std::cout << "Absent country: " << i->first << std::endl;
            }
        }
    }
    
private:
    std::map<std::string, int> code_countries_;
    std::map<std::string, int> name_countries_;
    std::map<std::string, int> url_countries_;
    std::map<std::string, int> code_locations_;
    std::vector<Coord> locations_;
    std::vector<Country> countries_;
};

int main(int argc, const char * argv[]) {
    RelationModel relation_model;
    
    if (!relation_model.LoadCodeLocation()) {
        return -1;
    }
    
    if (!relation_model.InitCountryList()) {
        return -1;
    }
    
    relation_model.Validate();
    
    if (!relation_model.ModelCountryRelation()) {
        return -1;
    }
    
    if (!relation_model.Serialize("relation.shp")) {
        return -1;
    }
    
    return 0;
}
