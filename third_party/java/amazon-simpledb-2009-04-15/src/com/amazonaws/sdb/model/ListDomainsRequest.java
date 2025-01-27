
package com.amazonaws.sdb.model;

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
 *         &lt;element name="MaxNumberOfDomains" type="{http://www.w3.org/2001/XMLSchema}int" minOccurs="0"/>
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
    "maxNumberOfDomains",
    "nextToken"
})
@XmlRootElement(name = "ListDomainsRequest")
public class ListDomainsRequest {

    @XmlElement(name = "MaxNumberOfDomains")
    protected Integer maxNumberOfDomains;
    @XmlElement(name = "NextToken")
    protected String nextToken;

    /**
     * Default constructor
     * 
     */
    public ListDomainsRequest() {
        super();
    }

    /**
     * Value constructor
     * 
     */
    public ListDomainsRequest(final Integer maxNumberOfDomains, final String nextToken) {
        this.maxNumberOfDomains = maxNumberOfDomains;
        this.nextToken = nextToken;
    }

    /**
     * Gets the value of the maxNumberOfDomains property.
     * 
     * @return
     *     possible object is
     *     {@link Integer }
     *     
     */
    public Integer getMaxNumberOfDomains() {
        return maxNumberOfDomains;
    }

    /**
     * Sets the value of the maxNumberOfDomains property.
     * 
     * @param value
     *     allowed object is
     *     {@link Integer }
     *     
     */
    public void setMaxNumberOfDomains(Integer value) {
        this.maxNumberOfDomains = value;
    }

    public boolean isSetMaxNumberOfDomains() {
        return (this.maxNumberOfDomains!= null);
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
     * Sets the value of the MaxNumberOfDomains property.
     * 
     * @param value
     * @return
     *     this instance
     */
    public ListDomainsRequest withMaxNumberOfDomains(Integer value) {
        setMaxNumberOfDomains(value);
        return this;
    }

    /**
     * Sets the value of the NextToken property.
     * 
     * @param value
     * @return
     *     this instance
     */
    public ListDomainsRequest withNextToken(String value) {
        setNextToken(value);
        return this;
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
        if (isSetMaxNumberOfDomains()) {
            if (!first) json.append(", ");
            json.append(quoteJSON("MaxNumberOfDomains"));
            json.append(" : ");
            json.append(quoteJSON(getMaxNumberOfDomains() + ""));
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
