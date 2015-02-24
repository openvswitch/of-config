<?xml version="1.0" standalone="yes"?>
<!--This XSLT was automatically generated from a Schematron schema.-->
<axsl:stylesheet xmlns:date="http://exslt.org/dates-and-times" xmlns:dyn="http://exslt.org/dynamic" xmlns:exsl="http://exslt.org/common" xmlns:math="http://exslt.org/math" xmlns:random="http://exslt.org/random" xmlns:regexp="http://exslt.org/regular-expressions" xmlns:set="http://exslt.org/sets" xmlns:str="http://exslt.org/strings" xmlns:axsl="http://www.w3.org/1999/XSL/Transform" xmlns:sch="http://www.ascc.net/xml/schematron" xmlns:iso="http://purl.oclc.org/dsdl/schematron" xmlns:of-config="urn:onf:config:yang" xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0" extension-element-prefixes="date dyn exsl math random regexp set str" version="1.0"><!--Implementers: please note that overriding process-prolog or process-root is 
    the preferred method for meta-stylesheets to use where possible. -->
<axsl:param name="archiveDirParameter"/><axsl:param name="archiveNameParameter"/><axsl:param name="fileNameParameter"/><axsl:param name="fileDirParameter"/>

<!--PHASES-->


<!--PROLOG-->
<axsl:output xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" xmlns:svrl="http://purl.oclc.org/dsdl/svrl" method="xml" omit-xml-declaration="no" standalone="yes" indent="yes"/>

<!--KEYS-->


<!--DEFAULT RULES-->


<!--MODE: SCHEMATRON-SELECT-FULL-PATH-->
<!--This mode can be used to generate an ugly though full XPath for locators-->
<axsl:template match="*" mode="schematron-select-full-path"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:template>

<!--MODE: SCHEMATRON-FULL-PATH-->
<!--This mode can be used to generate an ugly though full XPath for locators-->
<axsl:template match="*" mode="schematron-get-full-path"><axsl:apply-templates select="parent::*" mode="schematron-get-full-path"/><axsl:text>/</axsl:text><axsl:choose><axsl:when test="namespace-uri()=''"><axsl:value-of select="name()"/><axsl:variable name="p_1" select="1+    count(preceding-sibling::*[name()=name(current())])"/><axsl:if test="$p_1&gt;1 or following-sibling::*[name()=name(current())]">[<axsl:value-of select="$p_1"/>]</axsl:if></axsl:when><axsl:otherwise><axsl:text>*[local-name()='</axsl:text><axsl:value-of select="local-name()"/><axsl:text>' and namespace-uri()='</axsl:text><axsl:value-of select="namespace-uri()"/><axsl:text>']</axsl:text><axsl:variable name="p_2" select="1+   count(preceding-sibling::*[local-name()=local-name(current())])"/><axsl:if test="$p_2&gt;1 or following-sibling::*[local-name()=local-name(current())]">[<axsl:value-of select="$p_2"/>]</axsl:if></axsl:otherwise></axsl:choose></axsl:template><axsl:template match="@*" mode="schematron-get-full-path"><axsl:text>/</axsl:text><axsl:choose><axsl:when test="namespace-uri()=''">@<axsl:value-of select="name()"/></axsl:when><axsl:otherwise><axsl:text>@*[local-name()='</axsl:text><axsl:value-of select="local-name()"/><axsl:text>' and namespace-uri()='</axsl:text><axsl:value-of select="namespace-uri()"/><axsl:text>']</axsl:text></axsl:otherwise></axsl:choose></axsl:template>

<!--MODE: SCHEMATRON-FULL-PATH-2-->
<!--This mode can be used to generate prefixed XPath for humans-->
<axsl:template match="node() | @*" mode="schematron-get-full-path-2"><axsl:for-each select="ancestor-or-self::*"><axsl:text>/</axsl:text><axsl:value-of select="name(.)"/><axsl:if test="preceding-sibling::*[name(.)=name(current())]"><axsl:text>[</axsl:text><axsl:value-of select="count(preceding-sibling::*[name(.)=name(current())])+1"/><axsl:text>]</axsl:text></axsl:if></axsl:for-each><axsl:if test="not(self::*)"><axsl:text/>/@<axsl:value-of select="name(.)"/></axsl:if></axsl:template>

<!--MODE: GENERATE-ID-FROM-PATH -->
<axsl:template match="/" mode="generate-id-from-path"/><axsl:template match="text()" mode="generate-id-from-path"><axsl:apply-templates select="parent::*" mode="generate-id-from-path"/><axsl:value-of select="concat('.text-', 1+count(preceding-sibling::text()), '-')"/></axsl:template><axsl:template match="comment()" mode="generate-id-from-path"><axsl:apply-templates select="parent::*" mode="generate-id-from-path"/><axsl:value-of select="concat('.comment-', 1+count(preceding-sibling::comment()), '-')"/></axsl:template><axsl:template match="processing-instruction()" mode="generate-id-from-path"><axsl:apply-templates select="parent::*" mode="generate-id-from-path"/><axsl:value-of select="concat('.processing-instruction-', 1+count(preceding-sibling::processing-instruction()), '-')"/></axsl:template><axsl:template match="@*" mode="generate-id-from-path"><axsl:apply-templates select="parent::*" mode="generate-id-from-path"/><axsl:value-of select="concat('.@', name())"/></axsl:template><axsl:template match="*" mode="generate-id-from-path" priority="-0.5"><axsl:apply-templates select="parent::*" mode="generate-id-from-path"/><axsl:text>.</axsl:text><axsl:value-of select="concat('.',name(),'-',1+count(preceding-sibling::*[name()=name(current())]),'-')"/></axsl:template><!--MODE: SCHEMATRON-FULL-PATH-3-->
<!--This mode can be used to generate prefixed XPath for humans 
	(Top-level element has index)-->
<axsl:template match="node() | @*" mode="schematron-get-full-path-3"><axsl:for-each select="ancestor-or-self::*"><axsl:text>/</axsl:text><axsl:value-of select="name(.)"/><axsl:if test="parent::*"><axsl:text>[</axsl:text><axsl:value-of select="count(preceding-sibling::*[name(.)=name(current())])+1"/><axsl:text>]</axsl:text></axsl:if></axsl:for-each><axsl:if test="not(self::*)"><axsl:text/>/@<axsl:value-of select="name(.)"/></axsl:if></axsl:template>

<!--MODE: GENERATE-ID-2 -->
<axsl:template match="/" mode="generate-id-2">U</axsl:template><axsl:template match="*" mode="generate-id-2" priority="2"><axsl:text>U</axsl:text><axsl:number level="multiple" count="*"/></axsl:template><axsl:template match="node()" mode="generate-id-2"><axsl:text>U.</axsl:text><axsl:number level="multiple" count="*"/><axsl:text>n</axsl:text><axsl:number count="node()"/></axsl:template><axsl:template match="@*" mode="generate-id-2"><axsl:text>U.</axsl:text><axsl:number level="multiple" count="*"/><axsl:text>_</axsl:text><axsl:value-of select="string-length(local-name(.))"/><axsl:text>_</axsl:text><axsl:value-of select="translate(name(),':','.')"/></axsl:template><!--Strip characters--><axsl:template match="text()" priority="-1"/>

<!--SCHEMA METADATA-->
<axsl:template match="/"><svrl:schematron-output xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" title="" schemaVersion=""><axsl:comment><axsl:value-of select="$archiveDirParameter"/>   
		 <axsl:value-of select="$archiveNameParameter"/>  
		 <axsl:value-of select="$fileNameParameter"/>  
		 <axsl:value-of select="$fileDirParameter"/></axsl:comment><svrl:ns-prefix-in-attribute-values uri="http://exslt.org/dynamic" prefix="dyn"/><svrl:ns-prefix-in-attribute-values uri="urn:onf:config:yang" prefix="of-config"/><svrl:ns-prefix-in-attribute-values uri="urn:ietf:params:xml:ns:netconf:base:1.0" prefix="nc"/><svrl:active-pattern><axsl:attribute name="id">of-config</axsl:attribute><axsl:attribute name="name">of-config</axsl:attribute><axsl:apply-templates/></svrl:active-pattern><axsl:apply-templates select="/" mode="M4"/><svrl:active-pattern><axsl:attribute name="id">idm140214656138656</axsl:attribute><axsl:attribute name="name">idm140214656138656</axsl:attribute><axsl:apply-templates/></svrl:active-pattern><axsl:apply-templates select="/" mode="M5"/><svrl:active-pattern><axsl:attribute name="id">idm140214656137568</axsl:attribute><axsl:attribute name="name">idm140214656137568</axsl:attribute><axsl:apply-templates/></svrl:active-pattern><axsl:apply-templates select="/" mode="M6"/><svrl:active-pattern><axsl:attribute name="id">idm140214656136464</axsl:attribute><axsl:attribute name="name">idm140214656136464</axsl:attribute><axsl:apply-templates/></svrl:active-pattern><axsl:apply-templates select="/" mode="M7"/><svrl:active-pattern><axsl:attribute name="id">idm140214656135120</axsl:attribute><axsl:attribute name="name">idm140214656135120</axsl:attribute><axsl:apply-templates/></svrl:active-pattern><axsl:apply-templates select="/" mode="M8"/><svrl:active-pattern><axsl:attribute name="id">idm140214656134304</axsl:attribute><axsl:attribute name="name">idm140214656134304</axsl:attribute><axsl:apply-templates/></svrl:active-pattern><axsl:apply-templates select="/" mode="M9"/><svrl:active-pattern><axsl:attribute name="id">idm140214656071200</axsl:attribute><axsl:attribute name="name">idm140214656071200</axsl:attribute><axsl:apply-templates/></svrl:active-pattern><axsl:apply-templates select="/" mode="M10"/><svrl:active-pattern><axsl:attribute name="id">idm140214656063232</axsl:attribute><axsl:attribute name="name">idm140214656063232</axsl:attribute><axsl:apply-templates/></svrl:active-pattern><axsl:apply-templates select="/" mode="M11"/><svrl:active-pattern><axsl:attribute name="id">idm140214656111376</axsl:attribute><axsl:attribute name="name">idm140214656111376</axsl:attribute><axsl:apply-templates/></svrl:active-pattern><axsl:apply-templates select="/" mode="M12"/></svrl:schematron-output></axsl:template>

<!--SCHEMATRON PATTERNS-->
<axsl:param name="root" select="/nc:data"/>

<!--PATTERN of-config-->


	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:port" priority="1014" mode="M4"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:port"/>

		<!--REPORT -->
<axsl:if test="preceding-sibling::of-config:port[of-config:name=current()/of-config:name]"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="preceding-sibling::of-config:port[of-config:name=current()/of-config:name]"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate key "of-config:name"</svrl:text></svrl:successful-report></axsl:if>

		<!--ASSERT -->
<axsl:choose><axsl:when test="of-config:features/of-config:current/of-config:rate != 'other' or (count(of-config:current-rate) = 1 and count(of-config:max-rate) = 1 and  of-config:current-rate &gt; 0 and of-config:max-rate &gt; 0)"/><axsl:otherwise><svrl:failed-assert xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="of-config:features/of-config:current/of-config:rate != 'other' or (count(of-config:current-rate) = 1 and count(of-config:max-rate) = 1 and of-config:current-rate &gt; 0 and of-config:max-rate &gt; 0)"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>current-rate and max-rate must be specified and greater than 0 if rate
       equals 'other'</svrl:text></svrl:failed-assert></axsl:otherwise></axsl:choose><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:current-rate" priority="1013" mode="M4"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:current-rate"/>

		<!--ASSERT -->
<axsl:choose><axsl:when test="ancestor-or-self::*[processing-instruction('dsrl')] or (../of-config:features/of-config:current/of-config:rate='other')"/><axsl:otherwise><svrl:failed-assert xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="ancestor-or-self::*[processing-instruction('dsrl')] or (../of-config:features/of-config:current/of-config:rate='other')"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Node "of-config:current-rate" is only valid when "../of-config:features/of-config:current/of-config:rate='other'"</svrl:text></svrl:failed-assert></axsl:otherwise></axsl:choose><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:max-rate" priority="1012" mode="M4"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:max-rate"/>

		<!--ASSERT -->
<axsl:choose><axsl:when test="ancestor-or-self::*[processing-instruction('dsrl')] or (../of-config:features/of-config:current/of-config:rate='other')"/><axsl:otherwise><svrl:failed-assert xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="ancestor-or-self::*[processing-instruction('dsrl')] or (../of-config:features/of-config:current/of-config:rate='other')"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Node "of-config:max-rate" is only valid when "../of-config:features/of-config:current/of-config:rate='other'"</svrl:text></svrl:failed-assert></axsl:otherwise></axsl:choose><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:queue" priority="1011" mode="M4"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:queue"/>

		<!--REPORT -->
<axsl:if test="preceding-sibling::of-config:queue[of-config:resource-id=current()/of-config:resource-id]"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="preceding-sibling::of-config:queue[of-config:resource-id=current()/of-config:resource-id]"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate key "of-config:resource-id"</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:queue/of-config:port" priority="1010" mode="M4"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:queue/of-config:port"/>

		<!--REPORT -->
<axsl:if test="not($root/of-config:capable-switch/of-config:resources/of-config:port/of-config:name=.)"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="not($root/of-config:capable-switch/of-config:resources/of-config:port/of-config:name=.)"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Leaf "/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:name" does not exist for leafref value "<axsl:text/><axsl:value-of select="."/><axsl:text/>"</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:owned-certificate" priority="1009" mode="M4"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:owned-certificate"/>

		<!--REPORT -->
<axsl:if test="preceding-sibling::of-config:owned-certificate[of-config:resource-id=current()/of-config:resource-id]"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="preceding-sibling::of-config:owned-certificate[of-config:resource-id=current()/of-config:resource-id]"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate key "of-config:resource-id"</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:external-certificate" priority="1008" mode="M4"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:external-certificate"/>

		<!--REPORT -->
<axsl:if test="preceding-sibling::of-config:external-certificate[of-config:resource-id=current()/of-config:resource-id]"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="preceding-sibling::of-config:external-certificate[of-config:resource-id=current()/of-config:resource-id]"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate key "of-config:resource-id"</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:flow-table" priority="1007" mode="M4"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:flow-table"/>

		<!--REPORT -->
<axsl:if test="preceding-sibling::of-config:flow-table[of-config:resource-id=current()/of-config:resource-id]"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="preceding-sibling::of-config:flow-table[of-config:resource-id=current()/of-config:resource-id]"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate key "of-config:resource-id"</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch" priority="1006" mode="M4"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch"/>

		<!--REPORT -->
<axsl:if test="preceding-sibling::of-config:switch[of-config:id=current()/of-config:id]"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="preceding-sibling::of-config:switch[of-config:id=current()/of-config:id]"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate key "of-config:id"</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:controllers/of-config:controller" priority="1005" mode="M4"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:controllers/of-config:controller"/>

		<!--REPORT -->
<axsl:if test="preceding-sibling::of-config:controller[of-config:id=current()/of-config:id]"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="preceding-sibling::of-config:controller[of-config:id=current()/of-config:id]"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate key "of-config:id"</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:controllers/of-config:controller/of-config:state/of-config:supported-versions" priority="1004" mode="M4"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:controllers/of-config:controller/of-config:state/of-config:supported-versions"/>

		<!--REPORT -->
<axsl:if test=". = preceding-sibling::of-config:supported-versions"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test=". = preceding-sibling::of-config:supported-versions"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate leaf-list entry "<axsl:text/><axsl:value-of select="."/><axsl:text/>".</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:resources/of-config:port" priority="1003" mode="M4"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:resources/of-config:port"/>

		<!--REPORT -->
<axsl:if test=". = preceding-sibling::of-config:port"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test=". = preceding-sibling::of-config:port"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate leaf-list entry "<axsl:text/><axsl:value-of select="."/><axsl:text/>".</svrl:text></svrl:successful-report></axsl:if>

		<!--REPORT -->
<axsl:if test="not($root/of-config:capable-switch/of-config:resources/of-config:port/of-config:name=.)"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="not($root/of-config:capable-switch/of-config:resources/of-config:port/of-config:name=.)"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Leaf "/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:name" does not exist for leafref value "<axsl:text/><axsl:value-of select="."/><axsl:text/>"</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:resources/of-config:queue" priority="1002" mode="M4"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:resources/of-config:queue"/>

		<!--REPORT -->
<axsl:if test=". = preceding-sibling::of-config:queue"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test=". = preceding-sibling::of-config:queue"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate leaf-list entry "<axsl:text/><axsl:value-of select="."/><axsl:text/>".</svrl:text></svrl:successful-report></axsl:if>

		<!--REPORT -->
<axsl:if test="not($root/of-config:capable-switch/of-config:resources/of-config:queue/of-config:resource-id=.)"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="not($root/of-config:capable-switch/of-config:resources/of-config:queue/of-config:resource-id=.)"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Leaf "/nc:data/of-config:capable-switch/of-config:resources/of-config:queue/of-config:resource-id" does not exist for leafref value "<axsl:text/><axsl:value-of select="."/><axsl:text/>"</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:resources/of-config:certificate" priority="1001" mode="M4"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:resources/of-config:certificate"/>

		<!--REPORT -->
<axsl:if test="not($root/of-config:capable-switch/of-config:resources/of-config:owned-certificate/of-config:resource-id=.)"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="not($root/of-config:capable-switch/of-config:resources/of-config:owned-certificate/of-config:resource-id=.)"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Leaf "/nc:data/of-config:capable-switch/of-config:resources/of-config:owned-certificate/of-config:resource-id" does not exist for leafref value "<axsl:text/><axsl:value-of select="."/><axsl:text/>"</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:resources/of-config:flow-table" priority="1000" mode="M4"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:resources/of-config:flow-table"/>

		<!--REPORT -->
<axsl:if test=". = preceding-sibling::of-config:flow-table"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test=". = preceding-sibling::of-config:flow-table"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate leaf-list entry "<axsl:text/><axsl:value-of select="."/><axsl:text/>".</svrl:text></svrl:successful-report></axsl:if>

		<!--REPORT -->
<axsl:if test="not($root/of-config:capable-switch/of-config:resources/of-config:flow-table/of-config:resource-id=.)"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="not($root/of-config:capable-switch/of-config:resources/of-config:flow-table/of-config:resource-id=.)"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Leaf "/nc:data/of-config:capable-switch/of-config:resources/of-config:flow-table/of-config:resource-id" does not exist for leafref value "<axsl:text/><axsl:value-of select="."/><axsl:text/>"</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template><axsl:template match="text()" priority="-1" mode="M4"/><axsl:template match="@*|node()" priority="-2" mode="M4"><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M4"/></axsl:template>

<!--PATTERN idm140214656138656-->


	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:features/of-config:advertised/of-config:rate" priority="1001" mode="M5"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:features/of-config:advertised/of-config:rate"/>

		<!--REPORT -->
<axsl:if test=". = preceding-sibling::of-config:rate"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test=". = preceding-sibling::of-config:rate"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate leaf-list entry "<axsl:text/><axsl:value-of select="."/><axsl:text/>".</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M5"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:features/of-config:advertised/of-config:medium" priority="1000" mode="M5"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:features/of-config:advertised/of-config:medium"/>

		<!--REPORT -->
<axsl:if test=". = preceding-sibling::of-config:medium"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test=". = preceding-sibling::of-config:medium"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate leaf-list entry "<axsl:text/><axsl:value-of select="."/><axsl:text/>".</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M5"/></axsl:template><axsl:template match="text()" priority="-1" mode="M5"/><axsl:template match="@*|node()" priority="-2" mode="M5"><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M5"/></axsl:template>

<!--PATTERN idm140214656137568-->


	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:features/of-config:supported/of-config:rate" priority="1001" mode="M6"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:features/of-config:supported/of-config:rate"/>

		<!--REPORT -->
<axsl:if test=". = preceding-sibling::of-config:rate"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test=". = preceding-sibling::of-config:rate"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate leaf-list entry "<axsl:text/><axsl:value-of select="."/><axsl:text/>".</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M6"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:features/of-config:supported/of-config:medium" priority="1000" mode="M6"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:features/of-config:supported/of-config:medium"/>

		<!--REPORT -->
<axsl:if test=". = preceding-sibling::of-config:medium"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test=". = preceding-sibling::of-config:medium"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate leaf-list entry "<axsl:text/><axsl:value-of select="."/><axsl:text/>".</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M6"/></axsl:template><axsl:template match="text()" priority="-1" mode="M6"/><axsl:template match="@*|node()" priority="-2" mode="M6"><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M6"/></axsl:template>

<!--PATTERN idm140214656136464-->


	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:features/of-config:advertised-peer/of-config:rate" priority="1001" mode="M7"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:features/of-config:advertised-peer/of-config:rate"/>

		<!--REPORT -->
<axsl:if test=". = preceding-sibling::of-config:rate"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test=". = preceding-sibling::of-config:rate"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate leaf-list entry "<axsl:text/><axsl:value-of select="."/><axsl:text/>".</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M7"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:features/of-config:advertised-peer/of-config:medium" priority="1000" mode="M7"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:features/of-config:advertised-peer/of-config:medium"/>

		<!--REPORT -->
<axsl:if test=". = preceding-sibling::of-config:medium"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test=". = preceding-sibling::of-config:medium"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate leaf-list entry "<axsl:text/><axsl:value-of select="."/><axsl:text/>".</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M7"/></axsl:template><axsl:template match="text()" priority="-1" mode="M7"/><axsl:template match="@*|node()" priority="-2" mode="M7"><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M7"/></axsl:template>

<!--PATTERN idm140214656135120-->


	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:tunnel" priority="1000" mode="M8"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:tunnel"/>

		<!--ASSERT -->
<axsl:choose><axsl:when test="of-config:local-endpoint-ipv4-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-ipv4-adress[not(processing-instruction('dsrl'))] or of-config:local-endpoint-ipv6-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-ipv6-adress[not(processing-instruction('dsrl'))] or of-config:local-endpoint-mac-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-mac-adress[not(processing-instruction('dsrl'))] or false()"/><axsl:otherwise><svrl:failed-assert xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="of-config:local-endpoint-ipv4-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-ipv4-adress[not(processing-instruction('dsrl'))] or of-config:local-endpoint-ipv6-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-ipv6-adress[not(processing-instruction('dsrl'))] or of-config:local-endpoint-mac-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-mac-adress[not(processing-instruction('dsrl'))] or false()"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Node(s) from one case of mandatory choice "endpoints" must exist</svrl:text></svrl:failed-assert></axsl:otherwise></axsl:choose><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M8"/></axsl:template><axsl:template match="text()" priority="-1" mode="M8"/><axsl:template match="@*|node()" priority="-2" mode="M8"><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M8"/></axsl:template>

<!--PATTERN idm140214656134304-->


	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:ipgre-tunnel/of-config:key" priority="1000" mode="M9"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:ipgre-tunnel/of-config:key"/>

		<!--ASSERT -->
<axsl:choose><axsl:when test="ancestor-or-self::*[processing-instruction('dsrl')] or (../of-config:key-present='true')"/><axsl:otherwise><svrl:failed-assert xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="ancestor-or-self::*[processing-instruction('dsrl')] or (../of-config:key-present='true')"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Node "key" is only valid when "../of-config:key-present='true'"</svrl:text></svrl:failed-assert></axsl:otherwise></axsl:choose><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M9"/></axsl:template><axsl:template match="text()" priority="-1" mode="M9"/><axsl:template match="@*|node()" priority="-2" mode="M9"><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M9"/></axsl:template>

<!--PATTERN idm140214656071200-->


	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:vxlan-tunnel" priority="1000" mode="M10"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:vxlan-tunnel"/>

		<!--ASSERT -->
<axsl:choose><axsl:when test="of-config:local-endpoint-ipv4-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-ipv4-adress[not(processing-instruction('dsrl'))] or of-config:local-endpoint-ipv6-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-ipv6-adress[not(processing-instruction('dsrl'))] or of-config:local-endpoint-mac-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-mac-adress[not(processing-instruction('dsrl'))] or false()"/><axsl:otherwise><svrl:failed-assert xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="of-config:local-endpoint-ipv4-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-ipv4-adress[not(processing-instruction('dsrl'))] or of-config:local-endpoint-ipv6-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-ipv6-adress[not(processing-instruction('dsrl'))] or of-config:local-endpoint-mac-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-mac-adress[not(processing-instruction('dsrl'))] or false()"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Node(s) from one case of mandatory choice "endpoints" must exist</svrl:text></svrl:failed-assert></axsl:otherwise></axsl:choose><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M10"/></axsl:template><axsl:template match="text()" priority="-1" mode="M10"/><axsl:template match="@*|node()" priority="-2" mode="M10"><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M10"/></axsl:template>

<!--PATTERN idm140214656063232-->


	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:nvgre-tunnel" priority="1000" mode="M11"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:resources/of-config:port/of-config:nvgre-tunnel"/>

		<!--ASSERT -->
<axsl:choose><axsl:when test="of-config:local-endpoint-ipv4-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-ipv4-adress[not(processing-instruction('dsrl'))] or of-config:local-endpoint-ipv6-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-ipv6-adress[not(processing-instruction('dsrl'))] or of-config:local-endpoint-mac-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-mac-adress[not(processing-instruction('dsrl'))] or false()"/><axsl:otherwise><svrl:failed-assert xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test="of-config:local-endpoint-ipv4-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-ipv4-adress[not(processing-instruction('dsrl'))] or of-config:local-endpoint-ipv6-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-ipv6-adress[not(processing-instruction('dsrl'))] or of-config:local-endpoint-mac-adress[not(processing-instruction('dsrl'))] or of-config:remote-endpoint-mac-adress[not(processing-instruction('dsrl'))] or false()"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Node(s) from one case of mandatory choice "endpoints" must exist</svrl:text></svrl:failed-assert></axsl:otherwise></axsl:choose><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M11"/></axsl:template><axsl:template match="text()" priority="-1" mode="M11"/><axsl:template match="@*|node()" priority="-2" mode="M11"><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M11"/></axsl:template>

<!--PATTERN idm140214656111376-->


	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:capabilities/of-config:reserved-port-types/of-config:type" priority="1004" mode="M12"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:capabilities/of-config:reserved-port-types/of-config:type"/>

		<!--REPORT -->
<axsl:if test=". = preceding-sibling::of-config:type"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test=". = preceding-sibling::of-config:type"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate leaf-list entry "<axsl:text/><axsl:value-of select="."/><axsl:text/>".</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M12"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:capabilities/of-config:group-types/of-config:type" priority="1003" mode="M12"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:capabilities/of-config:group-types/of-config:type"/>

		<!--REPORT -->
<axsl:if test=". = preceding-sibling::of-config:type"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test=". = preceding-sibling::of-config:type"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate leaf-list entry "<axsl:text/><axsl:value-of select="."/><axsl:text/>".</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M12"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:capabilities/of-config:group-capabilities/of-config:capability" priority="1002" mode="M12"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:capabilities/of-config:group-capabilities/of-config:capability"/>

		<!--REPORT -->
<axsl:if test=". = preceding-sibling::of-config:capability"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test=". = preceding-sibling::of-config:capability"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate leaf-list entry "<axsl:text/><axsl:value-of select="."/><axsl:text/>".</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M12"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:capabilities/of-config:action-types/of-config:type" priority="1001" mode="M12"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:capabilities/of-config:action-types/of-config:type"/>

		<!--REPORT -->
<axsl:if test=". = preceding-sibling::of-config:type"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test=". = preceding-sibling::of-config:type"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate leaf-list entry "<axsl:text/><axsl:value-of select="."/><axsl:text/>".</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M12"/></axsl:template>

	<!--RULE -->
<axsl:template match="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:capabilities/of-config:instruction-types/of-config:type" priority="1000" mode="M12"><svrl:fired-rule xmlns:svrl="http://purl.oclc.org/dsdl/svrl" context="/nc:data/of-config:capable-switch/of-config:logical-switches/of-config:switch/of-config:capabilities/of-config:instruction-types/of-config:type"/>

		<!--REPORT -->
<axsl:if test=". = preceding-sibling::of-config:type"><svrl:successful-report xmlns:svrl="http://purl.oclc.org/dsdl/svrl" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:schold="http://www.ascc.net/xml/schematron" test=". = preceding-sibling::of-config:type"><axsl:attribute name="location"><axsl:apply-templates select="." mode="schematron-get-full-path"/></axsl:attribute><svrl:text>Duplicate leaf-list entry "<axsl:text/><axsl:value-of select="."/><axsl:text/>".</svrl:text></svrl:successful-report></axsl:if><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M12"/></axsl:template><axsl:template match="text()" priority="-1" mode="M12"/><axsl:template match="@*|node()" priority="-2" mode="M12"><axsl:apply-templates select="*|comment()|processing-instruction()" mode="M12"/></axsl:template></axsl:stylesheet>
