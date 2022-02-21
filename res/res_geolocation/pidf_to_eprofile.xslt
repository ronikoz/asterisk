<?xml version="1.0"?>
<xsl:stylesheet version="1.0"
	xmlns:ca="urn:ietf:params:xml:ns:pidf:geopriv10:civicAddr"
	xmlns:def="urn:ietf:params:xml:ns:pidf"
	xmlns:dm="urn:ietf:params:xml:ns:pidf:data-model"
	xmlns:fn="http://www.w3.org/2005/xpath-functions"
	xmlns:gbp="urn:ietf:params:xml:ns:pidf:geopriv10:basicPolicy"
	xmlns:gml="http://www.opengis.net/gml"
	xmlns:gp="urn:ietf:params:xml:ns:pidf:geopriv10"
	xmlns:gs="http://www.opengis.net/pidflo/1.0"
	xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
	<!--xsl:output method="text" /-->
	<xsl:strip-space elements="*" />
	<xsl:param name="path"/>

	<xsl:template name="name-value">
		<xsl:value-of select="local-name(.)" /><xsl:text>="</xsl:text>
		<xsl:value-of select="normalize-space(.)" /><xsl:text>"</xsl:text>
		<xsl:if test="following-sibling::*"><xsl:text>, </xsl:text></xsl:if>
	</xsl:template>

	<xsl:template name="length"><xsl:call-template name="name-value" /></xsl:template>

	<xsl:template name="angle">
		<xsl:value-of select="local-name(.)" /><xsl:text>="</xsl:text>
		<xsl:value-of select="normalize-space(.)" /><xsl:text>"</xsl:text><xsl:text>, </xsl:text>
		<xsl:value-of select="local-name(.)" /><xsl:text>_uom="</xsl:text>
		<xsl:choose>
			<xsl:when test="@uom = 'urn:ogc:def:uom:EPSG::9102'"><xsl:text>radians</xsl:text></xsl:when>
			<xsl:otherwise><xsl:text>degrees</xsl:text></xsl:otherwise>
		</xsl:choose>
		<xsl:text>"</xsl:text>
		<xsl:if test="following-sibling::*"><xsl:text>, </xsl:text></xsl:if>
	</xsl:template>
	<xsl:template match="gs:orientation"><xsl:call-template name="angle" /></xsl:template>
	<xsl:template match="gs:radius"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="gs:height"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="gs:semiMajorAxis"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="gs:semiMinorAxis"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="gs:verticalAxis"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="gs:innerRadius"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="gs:outerRadius"><xsl:call-template name="length" /></xsl:template>
	<xsl:template match="gs:startAngle"><xsl:call-template name="angle" /></xsl:template>
	<xsl:template match="gs:openingAngle"><xsl:call-template name="angle" /></xsl:template>
	<xsl:template match="gml:pos"><xsl:call-template name="name-value" /></xsl:template>
	<xsl:template match="gml:posList"><xsl:call-template name="name-value" /></xsl:template>

	<xsl:template name="shape">
		<xsl:text>format="gml", type="</xsl:text><xsl:value-of select="local-name(.)" /><xsl:text>", </xsl:text>
		<xsl:text>crs="</xsl:text>
		<xsl:choose>
			<xsl:when test="@srsName = 'urn:ogc:def:crs:EPSG::4326'"><xsl:text>2d</xsl:text></xsl:when>
			<xsl:when test="@srsName = 'urn:ogc:def:crs:EPSG::4979'"><xsl:text>3d</xsl:text></xsl:when>
			<xsl:otherwise><xsl:text>unknown</xsl:text></xsl:otherwise>
		</xsl:choose>
		<xsl:text>", </xsl:text>
		<xsl:apply-templates />
	</xsl:template>

	<xsl:template match="ca:civicAddress/*"><xsl:call-template name="name-value" />	</xsl:template>
	<xsl:template name="civicAddress"><xsl:text>format="civicAddress", </xsl:text>
		<xsl:apply-templates />
	</xsl:template>

	<xsl:template match="gp:location-info/gml:*">
		<xsl:element name="location-info">
			<xsl:attribute name="format">gml</xsl:attribute>
			<xsl:call-template name="shape" />
		</xsl:element>
	</xsl:template>

	<xsl:template match="gp:location-info/gs:*">
		<xsl:element name="location-info">
			<xsl:attribute name="format">gml</xsl:attribute>
			<xsl:call-template name="shape" />
		</xsl:element>
	</xsl:template>

	<xsl:template match="gp:location-info/ca:*">
		<xsl:element name="location-info">
			<xsl:attribute name="format">civicAddress</xsl:attribute>
			<xsl:call-template name="civicAddress" />
		</xsl:element>
	</xsl:template>

	<xsl:template match="gp:usage-rules/*">
		<xsl:call-template name="name-value" />
	</xsl:template>

	<xsl:template match="gp:usage-rules">
		<xsl:element name="usage-rules">
			<xsl:apply-templates />
		</xsl:element>
	</xsl:template>

	<xsl:template match="gp:method">
		<xsl:value-of select="normalize-space(.)" />
	</xsl:template>

	<xsl:template name="topnode">
		<xsl:element name="pidf-element">
			<xsl:attribute name="name"><xsl:value-of select="local-name(.)"/></xsl:attribute>
			<xsl:attribute name="id"><xsl:value-of select="@id"/></xsl:attribute>
			<xsl:apply-templates select=".//gp:location-info"/>
			<xsl:apply-templates select=".//gp:usage-rules"/>
			<xsl:element name="method">
			<xsl:apply-templates select=".//gp:method"/>
			</xsl:element>
		</xsl:element>
		<xsl:text>
</xsl:text>
	</xsl:template>

	<xsl:template match="dm:device"><xsl:call-template name="topnode" /></xsl:template>
	<xsl:template match="def:tuple"><xsl:call-template name="topnode" /></xsl:template>
	<xsl:template match="dm:person"><xsl:call-template name="topnode" /></xsl:template>

	<xsl:template match="/">
		<xsl:element name="presence">
			<xsl:attribute name="entity"><xsl:value-of select="/*/@entity"/></xsl:attribute>
			<xsl:apply-templates select="$path"/>
		</xsl:element>
	</xsl:template>
</xsl:stylesheet>
