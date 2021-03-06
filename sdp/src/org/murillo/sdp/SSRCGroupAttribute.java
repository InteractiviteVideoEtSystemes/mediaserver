/*
 * To change this template, choose Tools | Templates
 * and open the template in the editor.
 */

package org.murillo.sdp;

import java.util.ArrayList;

/**
 *
 * @author Sergio
 */
public class SSRCGroupAttribute implements Attribute {

    private String semantics;
    private ArrayList<String> SSRCIds;

    public SSRCGroupAttribute() {
    }

    public SSRCGroupAttribute(String semantics) {
        this.semantics = semantics;
    }

    @Override
    public String toString() {
        //Get value
        String value = getValue();
        //Generic attr
        return "a="+ getField() + (value!=null ? ":" + value : "") + "\r\n";
    }

    @Override
    public String getField() {
        return "ssrc-group";
    }

    @Override
    public String getValue() {
        String value = semantics;
        if (SSRCIds != null)
        {
            for (String id : SSRCIds)
                value += " " + id;
        }
        return value;
    }

    public ArrayList<String> getSSRCIds() {
        return SSRCIds;
    }

    public void setSSRCIds(ArrayList<String> SSRCIds) {
        this.SSRCIds = SSRCIds;
    }
    
   public void addSSRCId(String ssrcId) {
       if (SSRCIds == null) SSRCIds = new ArrayList<String>();
       SSRCIds.add(ssrcId);
   }

    public String getSemantics() {
        return semantics;
    }

    public void setSemantics(String semantics) {
        this.semantics = semantics;
    }


}
