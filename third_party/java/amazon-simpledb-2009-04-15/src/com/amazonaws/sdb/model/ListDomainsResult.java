
package com.amazonaws.sdb.model;

import java.util.ArrayList;
import java.util.List;
import javax.xml.bind.annotation.XmlAccessType;
import javax.xml.bind.annotation.XmlAccessorType;
import javax.xml.bind.annotation.XmlElement;
import javax.xml.bind.annotation.XmlRootElement;
import javax.xml.bind.annotation.XmlType;


/**
 * <p>Java class for anonymous complex type.
 * 
 * <p>The following schema fragment specifies the expected content contained within this class.
 * 
 * <pre>
 * &lt;complexType>
 *   &lt;complexContent>
 *     &lt;restriction base="{http://www.w3.org/2001/XMLSchema}anyType">
 *       &lt;sequence>
 *         &lt;element name="DomainName" type="{http://www.w3.org/2001/XMLSchema}string" maxOccurs="unbounded" minOccurs="0"/>
 *         &lt;element name="NextToken" type="{http://www.w3.org/2001/XMLSchema}string" minOccurs="0"/>
 *       &lt;/sequence>
 *     &lt;/restriction>
 *   &lt;/complexContent>
 * &lt;/complexType>
 * </pre>
 * Generated by AWS Code Generator
 * <p/>
 * Mon May 11 14:17:05 PDT 2009
 * 
 */
@XmlAccessorType(XmlAccessType.FIELD)
@XmlType(name = "", propOrder = {
    "domainName",
    "nextToken"
})
@XmlRootElement(name = "ListDomainsResult")
public class ListDomainsResult {

    @XmlElement(name = "DomainName")
    protected List<String> domainName;
    @XmlElement(name = "NextToken")
    protected String nextToken;

    /**
     * Default constructor
     * 
     */
    public ListDomainsResult() {
        super();
    }

    /**
     * Value constructor
     * 
     */
    public ListDomainsResult(final List<String> domainName, final String nextToken) {
        this.domainName = domainName;
        this.nextToken = nextToken;
    }

    /**
     * Gets the value of the domainName property.
     * 
     * <p>
     * This accessor method returns a reference to the live list,
     * not a snapshot. Therefore any modification you make to the
     * returned list will be present inside the JAXB object.
     * This is why there is not a <CODE>set</CODE> method for the domainName property.
     * 
     * <p>
     * For example, to add a new item, do as follows:
     * <pre>
     *    getDomainName().add(newItem);
     * </pre>
     * 
     * 
     * <p>
     * Objects of the following type(s) are allowed in the list
     * {@link String }
     * 
     * 
     */
    public List<String> getDomainName() {
        if (domainName == null) {
            domainName = new ArrayList<String>();
        }
        return this.domainName;
    }

    public boolean isSetDomainName() {
        return ((this.domainName!= null)&&(!this.domainName.isEmpty()));
    }

    public void unsetDomainName() {
        this.domainName = null;
    }

    /**
     * Gets the value of the nextToken property.
     * 
     * @return
     *     possible object is
     *     {@link String }
     *     
     */
    public String getNextToken() {
        return nextToken;
    }

    /**
     * Sets the value of the nextToken property.
     * 
     * @param value
     *     allowed object is
     *     {@link String }
     *     
     */
    public void setNextToken(String value) {
        this.nextToken = value;
    }

    public boolean isSetNextToken() {
        return (this.nextToken!= null);
    }

    /**
     * Sets the value of the DomainName property.
     * 
     * @param values
     * @return
     *     this instance
     */
    public ListDomainsResult withDomainName(String... values) {
        for (String value: values) {
            getDomainName().add(value);
        }
        return this;
    }

    /**
     * Sets the value of the NextToken property.
     * 
     * @param value
     * @return
     *     this instance
     */
    public ListDomainsResult withNextToken(String value) {
        setNextToken(value);
        return this;
    }

    /**
     * Sets the value of the domainName property.
     * 
     * @param domainName
     *     allowed object is
     *     {@link String }
     *     
     */
    public void setDomainName(List<String> domainName) {
        this.domainName = domainName;
    }
    

    /**
     * 
     * XML fragment representation of this object
     * 
     * @return XML fragment for this object. Name for outer
     * tag expected to be set by calling method. This fragment
     * returns inner properties representation only
     */
    protected String toXMLFragment() {
        StringBuffer xml = new StringBuffer();
        java.util.List<String> domainNameList  =  getDomainName();
        for (String domainName : domainNameList) { 
            xml.append("<DomainName>");
            xml.append(escapeXML(domainName));
            xml.append("</DomainName>");
        }	
        if (isSetNextToken()) {
            xml.append("<NextToken>");
            xml.append(escapeXML(getNextToken()));
            xml.append("</NextToken>");
        }
        return xml.toString();
    }

    /**
     * 
     * Escape XML special characters
     */
    private String escapeXML(String string) {
        StringBuffer sb = new StringBuffer();
        int length = string.length();
        for (int i = 0; i < length; ++i) {
            char c = string.charAt(i);
            switch (c) {
            case '&':
                sb.append("&amp;");
                break;
            case '<':
                sb.append("&lt;");
                break;
            case '>':
                sb.append("&gt;");
                break;
            case '\'':
                sb.append("&#039;");
                break;
            case '"':
                sb.append("&quot;");
                break;
            default:
                sb.append(c);
            }
        }
        return sb.toString();
    }



    /**
     *
     * JSON fragment representation of this object
     *
     * @return JSON fragment for this object. Name for outer
     * object expected to be set by calling method. This fragment
     * returns inner properties representation only
     *
     */
    protected String toJSONFragment() {
        StringBuffer json = new StringBuffer();
        boolean first = true;
        if (isSetDomainName()) {
            if (!first) json.append(", ");
            json.append("\"DomainName\" : [");
            java.util.List<String> domainNameList  =  getDomainName();
            int domainNameListIndex  =  0;
            for (String domainName : domainNameList) {
                if (domainNameListIndex > 0) json.append(", ");
                    json.append(quoteJSON(domainName));
                ++domainNameListIndex;
            }
            json.append("]");
            first = false;
        }
        if (isSetNextToken()) {
            if (!first) json.append(", ");
            json.append(quoteJSON("NextToken"));
            json.append(" : ");
            json.append(quoteJSON(getNextToken()));
            first = false;
        }
        return json.toString();
    }

    /**
     *
     * Quote JSON string
     */
    private String quoteJSON(String string) {
        StringBuffer sb = new StringBuffer();
        sb.append("\"");
        int length = string.length();
        for (int i = 0; i < length; ++i) {
            char c = string.charAt(i);
            switch (c) {
            case '"':
                sb.append("\\\"");
                break;
            case '\\':
                sb.append("\\\\");
                break;
            case '/':
                sb.append("\\/");
                break;
            case '\b':
                sb.append("\\b");
                break;
            case '\f':
                sb.append("\\f");
                break;
            case '\n':
                sb.append("\\n");
                break;
            case '\r':
                sb.append("\\r");
                break;
            case '\t':
                sb.append("\\t");
                break;
            default:
                if (c <  ' ') {
                    sb.append("\\u" + String.format("%03x", Integer.valueOf(c)));
                } else {
                sb.append(c);
            }
        }
        }
        sb.append("\"");
        return sb.toString();
    }


}
